
/*--------------------------------------------------------------------*/
/*--- Pagein: The pagein times.                          pg_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   Copyright (C) 2005, 2006, 2012, 2019 Ulrich Drepper <drepper@redhat.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
*/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#if defined __i386__ || defined __x86_64__
# include <x86intrin.h>
#endif

#include <valgrind/pub_tool_basics.h>
#include <valgrind/pub_tool_hashtable.h>
#include <valgrind/pub_tool_debuginfo.h>
#include <valgrind/pub_tool_mallocfree.h>
#include <valgrind/pub_tool_tooliface.h>
#include <valgrind/pub_tool_libcassert.h>
#include <valgrind/pub_tool_libcbase.h>
#include <valgrind/pub_tool_libcfile.h>
#include <valgrind/pub_tool_libcprint.h>
#include <valgrind/pub_tool_libcproc.h>
#include <valgrind/pub_tool_machine.h>
#include <valgrind/pub_tool_vki.h>


#define PAGE_MASK(addr) ((addr) & ~(VKI_PAGE_SIZE - 1))

#define vgPlain_malloc(size) vgPlain_malloc ((const char *) __func__, size)

static VgHashTable *ht;
static const HChar *base_dir;


static void
pg_post_clo_init (void)
{
}


struct pageaddr_order
{
  VgHashNode top;
  unsigned int codefault;
  unsigned long int count;
  unsigned long long int ticks;
  HChar where[0];
};


static void
newpage (bool codefault, Addr dataaddr, Addr lastaddr)
{
  Addr pageaddr;
  if (codefault)
    pageaddr = PAGE_MASK (lastaddr);
  else
    pageaddr = PAGE_MASK (dataaddr);

  if (VG_(HT_lookup) (ht, pageaddr) == NULL)
    {
      static unsigned long int total;

      __auto_type cur_ep = VG_(current_DiEpoch) ();
      __auto_type buf = VG_(describe_IP) (cur_ep, lastaddr, NULL);
      size_t len = VG_(strlen) (buf) + 1;

      struct pageaddr_order *pa = VG_(malloc) (sizeof (*pa) + len);
      pa->top.key = pageaddr;
      pa->codefault = codefault;
      pa->count = __atomic_fetch_add (&total, 1, __ATOMIC_RELAXED);
#if defined __i386__ || defined __x86_64__
      pa->ticks = __rdtsc ();
#else
      pa->ticks = 0;
#endif
      VG_(strcpy) (pa->where, buf);

      VG_(HT_add_node) (ht, (VgHashNode *) pa);
    }
};


static void
VG_REGPARM (1)
newcodepage (Addr addr)
{
  newpage (true, 0, addr);
}


static void
VG_REGPARM (2)
newdatapage (Addr dataaddr, Addr lastaddr)
{
  static Addr pageaddr;

  if (PAGE_MASK (dataaddr) != pageaddr)
    {
      pageaddr = PAGE_MASK (dataaddr);
      newpage (false, dataaddr, lastaddr);
    }
}


static IRSB *
pg_instrument (VgCallbackClosure *closure __attribute__((unused)), IRSB *bbIn,
               const VexGuestLayout *layout __attribute__((unused)),
               const VexGuestExtents *vge __attribute__((unused)),
               const VexArchInfo *archinfo_host __attribute__((unused)),
               IRType gWordTy __attribute__((unused)),
               IRType hWordTy __attribute__((unused)))
{
  /* Set up BB.  */
  IRSB *bbOut = deepCopyIRSBExceptStmts (bbIn);

  /* Copy input to output while insert code to keep track of new page
     uses.  */
  Addr pageaddr = 0;
  Addr lastaddr = 0;
  bool first = true;
  for (Int i = 0; i < bbIn->stmts_used; ++i)
    {
      IRStmtTag tag = bbIn->stmts[i]->tag;

      if (tag == Ist_IMark)
        {
          lastaddr = bbIn->stmts[i]->Ist.IMark.addr;

          if (first || PAGE_MASK (lastaddr) != pageaddr)
            {
              IRDirty *di = unsafeIRDirty_0_N (1, "newcodepage",
                                               VG_(fnptr_to_fnentry) (&newcodepage),
                                               mkIRExprVec_1 (mkIRExpr_HWord ((HWord) lastaddr)));
              addStmtToIRSB (bbOut, IRStmt_Dirty (di));
              pageaddr = PAGE_MASK (lastaddr);
            }
        }
      else if (tag != Ist_NoOp && tag != Ist_AbiHint)
        {
          addStmtToIRSB (bbOut, bbIn->stmts[i]);

          if (tag == Ist_Store)
            {
              IRDirty *di = unsafeIRDirty_0_N (2, "newdatapage",
                                               VG_(fnptr_to_fnentry) (&newdatapage),
                                               mkIRExprVec_2 (bbIn->stmts[i]->Ist.Store.addr,
                                                              mkIRExpr_HWord ((HWord) lastaddr)));
              addStmtToIRSB (bbOut, IRStmt_Dirty (di));
            }
        }
    }

  return bbOut;
}


static Int
rescompare (const void *p1, const void *p2)
{
  __auto_type a1 = (const struct pageaddr_order **) p1;
  __auto_type a2 = (const struct pageaddr_order **) p2;

  if ((*a1)->count < (*a2)->count)
    return -1;
  if ((*a1)->count > (*a2)->count)
    return 1;
  return 0;
}

void pg_fini (Int exitcode __attribute__((unused)))
{
  struct pageaddr_order **res = VG_(malloc) (VG_(HT_count_nodes) (ht) * sizeof (*res));

  VG_(HT_ResetIter) (ht);
  VgHashNode *nd;
  int nres = 0;
  while ( (nd = VG_(HT_Next) (ht)) )
    res[nres++] = (struct pageaddr_order *) nd;

  VG_(ssort) (res, nres, sizeof (res[0]), rescompare);

  HChar fname[VG_(strlen) (base_dir) + sizeof ("/pagein.") + sizeof (pid_t) * 3];
  VG_(snprintf) (fname, sizeof(fname), "%s/pagein.%d", base_dir, VG_(getpid) ());

  SysRes fdres = VG_(open) (fname, VKI_O_CREAT|VKI_O_TRUNC|VKI_O_WRONLY,
                            VKI_S_IRUSR|VKI_S_IWUSR);
  if (sr_isError (fdres))
    return;
  int fd = sr_Res (fdres);

  for (int i = 0; i < nres; ++i)
    {
      HChar buf[1024];
      Int len = VG_(snprintf) (buf, sizeof (buf), "%4d %#0*lx %c %12llu %s\n",
                               i, (int)(2 + 2 * sizeof (void*)), res[i]->top.key,
                               res[i]->codefault ? 'C' : 'D',
                               res[i]->ticks - res[0]->ticks, res[i]->where);
      VG_(write) (fd, buf, len);
    }

  VG_(close) (fd);

  VG_(free) (res);
}


static
void pg_pre_clo_init (void)
{
   VG_(details_name)            ("Pagein");
   VG_(details_version)         (VERSION);
   VG_(details_description)     ("determine page-in order");
   VG_(details_copyright_author)(
      "Copyright (C) 2005, 2006, 2007, 2012, 2019 and GNU GPL'd, by Ulrich Drepper.");
   VG_(details_bug_reports_to)  ("drepper@redhat.com");

   VG_(basic_tool_funcs)        (pg_post_clo_init,
                                 pg_instrument,
                                 pg_fini);

   /* No needs, no core events to track */

  ht = VG_(HT_construct) ("ht");

  base_dir = VG_(get_startup_wd) ();
}


VG_DETERMINE_INTERFACE_VERSION (pg_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/
