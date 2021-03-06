/****************************************************************************
 *                                                                          *
 *     Loki - Programs for genetic analysis of complex traits using MCMC    *
 *                                                                          *
 *             Simon Heath - Rockefeller University                         *
 *                                                                          *
 *                       November 1997                                      *
 *                                                                          *
 * loki_tlmoves.c:                                                          *
 *                                                                          *
 * Lots of the more weird update moves                                      *
 *                                                                          *
 * Copyright (C) Simon C. Heath 1997, 2000, 2002                            *
 * This is free software.  You can distribute it and/or modify it           *
 * under the terms of the Modified BSD license, see the file COPYING        *
 *                                                                          *
 ****************************************************************************/

#include <config.h>
#include <stdlib.h>
#ifdef USE_DMALLOC
#include <dmalloc.h>
#endif
#include <math.h>
#include <stdio.h>
#include <float.h>
#ifndef DBL_MAX
#define DBL_MAX MAXDOUBLE
#endif
#include <assert.h>

#include "ranlib.h"
#include "utils.h"
#include "loki.h"
#include "loki_peel.h"
#include "loki_utils.h"
#include "loki_tlmoves.h"
#include "handle_res.h"
#include "sample_cens.h"
#include "lk_malloc.h"

static int *perm;
static struct Locus **locilist;
static double *prior,*new_freq;

double safe_exp(double x)
{
  static double max_arg;
  static char f=0;
	
  if(!f) {
    max_arg=log(DBL_MAX);
    f=1;
  }
  if(x>max_arg) return DBL_MAX;
  if(x<-max_arg) return 0.0;
  return exp(x);
}

static void Adjust_for_TL(const struct Locus *loc,const double z,const struct loki *loki)
{
  int i,j,k,type;
  double *eff;
  int *gt;
  struct Id_Record *id;
	
  eff=loc->eff[0];
  gt=loc->gt;
  type=loki->models->models[0].var.type;
  id=loki->pedigree->id_array;
  if(type&ST_CONSTANT)	{
    for(i=0;i<loki->pedigree->ped_size;i++,id++) if(id->res[0]) {
      k=gt[i]-1;
      if(k) id->res[0][0]+=z*eff[k-1];
    }
  } else for(i=0;i<loki->pedigree->ped_size;i++,id++) if(id->res[0]) {
    k=gt[i]-1;
    if(k) for(j=0;j<id->n_rec;j++) id->res[0][j]+=z*eff[k-1];
  }
}

static void Adjust_Mean(const double adj,const struct loki *loki)
{
  int i,j,type;
  struct Id_Record *id;
	
  type=loki->models->models[0].var.type;
  id=loki->pedigree->id_array;
  if(type&ST_CONSTANT)	{
    for(i=0;i<loki->pedigree->ped_size;i++,id++) if(id->res[0]) id->res[0][0]+=adj;
  } else for(i=0;i<loki->pedigree->ped_size;i++,id++) if(id->res[0]) for(j=0;j<id->n_rec;j++) id->res[0][j]+=adj;
  loki->models->grand_mean[0]-=adj;
}

static double interval_size(int j,int k,struct Link *lk,int sex_map,struct Locus **pm)
{
  int k2;
  double s=0.0,x;
	
  for(k2=0;k2<=sex_map;k2++)	{
    if(!j) {
      x=pm[0]->pos[k2];
      s+=x-lk->r1[k2];
    } else if(j==k) {
      x=pm[k-1]->pos[k2];
      s+=lk->r2[k2]-x;
    } else {
      x=pm[j]->pos[k2]-pm[j-1]->pos[k2];
      s+=x;
    }
  }
  return s;
}

struct Locus **get_sorted_locuslist(const int link,int *count,int flag)
{
  get_locuslist(locilist,link,count,flag);
  gnu_qsort(locilist,(size_t)*count,(size_t)sizeof(void *),cmp_loci);
  return locilist;
}

static double check_intervals(int j,int k,int link,struct Locus **pm,double *p,double move_p,const struct loki *loki)
{
  int sex_map,k1;
  double z,z1;
  struct Link *lk;
	
  lk=loki->markers->linkage+link;
  z=(1.0-move_p)*.5;
  p[1]=move_p;
  /* Check if we can move to neighbouring intervals */
  p[0]=j?z:0.0;
  p[2]=j<k?z:0.0;
  /* Are there intervals outside the markers? */
  if(j==1) {
    z=pm[0]->pos[0];
    if(z<=lk->r1[0]) p[0]=0.0;
  }
  if(j==(k-1)) {
    z=pm[k]->pos[0];
    if(z>=lk->r2[0]) p[2]=0.0;
  }
  sex_map=loki->markers->sex_map;
  if(p[0]>0.0) p[0]*=interval_size(j-1,k,lk,sex_map,pm);
  p[1]*=(z1=interval_size(j,k,lk,sex_map,pm));
  if(p[2]>0.0) p[2]*=interval_size(j+1,k,lk,sex_map,pm);
  z=p[0]+p[1]+p[2];
  for(k1=0;k1<3;k1++) p[k1]/=z;
  return z1;
}

#if 0
static void sample_mpos(const int link,const struct loki *loki) 
{
  int i,j,k2=-1,k3,ids,idd,sx,**seg,s,s1;
  double pp[2][50],ct1[2][50],ct2[2][50],x,x1,r,z,z1;
  struct Locus *loc;
  struct Id_Record *id;
	
  get_locuslist(locilist,link,&k3,0);
  gnu_qsort(locilist,(size_t)k3,(size_t)sizeof(void *),cmp_loci);
  for(i=0;i<k3;i++) {
    loc=locilist[i];
    if(loc->flag&LOCUS_SAMPLED) break;
    ct1[0][i]=ct1[1][i]=0.0;
    ct2[0][i]=ct2[1][i]=0.0;
  }
  if(i<k3) return;
  z=ct1[0][0]+ct1[1][0];
  z1=ct2[0][0]+ct2[1][0];
  id=loki->pedigree->id_array;
  for(i=0;i<loki->pedigree->ped_size;i++,id++) {
    ids=id->sire;
    if(!ids) continue;
    idd=id->dam;
    for(sx=0;sx<2;sx++) {
      for(j=0;j<k3;j++) {
	loc=locilist[j];
	seg=loc->seg;
	x=loc->pos[sx];
	k2=seg[sx][i];
	if(k2<0) {
	  if(j) {
	    r=.5*(1.0-exp(.02*(x1-x)));
	    pp[0][j]=pp[0][j-1]*(1.0-r)+pp[1][j-1]*r;
	    pp[1][j]=pp[1][j-1]*(1.0-r)+pp[0][j-1]*r;
	  } else pp[0][j]=pp[1][j]=0.5;
	} else {
	  assert(k2==0 || k2==1);
	  pp[k2][j]=1.0;
	  pp[1-k2][j]=0.0;
	}
	x1=x;
      }
      if(k2<0) {
	z=ranf()*(pp[0][k3-1]+pp[1][k3-1]);
	s=(z<=pp[0][k3-1]?0:1);
      } else s=k2;
      for(j=k3-2;j>=0;j--) {
	s1=s;
	x1=x;
	loc=locilist[j];
	seg=loc->seg;
	x=loc->pos[sx];
	k2=seg[sx][i];
	if(k2<0) {
	  r=.5*(1.0-exp(.02*(x-x1)));
	  pp[s1][j]*=1.0-r;
	  pp[1-s1][j]*=r;
	  z=ranf()*(pp[0][j]+pp[1][j]);
	  s=(z<=pp[0][j]?0:1);
	} else {
	  s=k2;
	}
	if(s!=s1) ct1[sx][j]+=1.0;
	else ct2[sx][j]+=1.0;
      }
    }
  }
  if(loki->markers->sex_map) {
    for(sx=0;sx<2;sx++) {
      for(j=0;j<k3;j++) {
	if(!j) x=locilist[0]->pos[sx];
	else {
	  do {
	    r=genbet(ct1[sx][j-1]+1.0,ct2[sx][j-1]+1.0);
	  } while(r>=0.5);
	  x-=50.0*log(1.0-2.0*r);
	  locilist[j]->pos[sx]=x;
	}
      }
    }
  } else {
    for(j=0;j<k3;j++) {
      if(!j)  x=locilist[0]->pos[0];
      else {
	z=ct1[0][j-1]+ct1[1][j-1];
	z1=ct2[0][j-1]+ct2[1][j-1];
	do {
	  r=genbet(ct1[0][j-1]+ct1[1][j-1]+1.0,ct2[0][j-1]+ct2[1][j-1]+1.0);
	} while(r>=0.5);
	x-=50.0*log(1.0-2.0*r);
	locilist[j]->pos[0]=locilist[j]->pos[1]=x;
      }
    }
  }
}
#endif

/* Sample genotypes for all marker loci in linkage group - trait loci are sampled during
 * movement step - see Sample_TL_Position() */
void Sample_LinkageGroup(const int link,struct loki *loki)
{
  int i,j,k2,k3;
  struct peel_mem *work;
	
#ifndef NDEBUG
  if((loki->params.debug_level)&4) (void)printf("[S:%d]",link);
  (void)fflush(stdout);
#endif
  /*   	sample_mpos(link); */
  work=&loki->peel->workspace;
  k2=loki->markers->linkage[link].n_markers+loki->params.n_tloci;
  get_locuslist(locilist,link,&k3,0);
  gnu_qsort(locilist,(size_t)k3,sizeof(void *),cmp_loci);
  for(i=0;i<k3;i++) perm[i]=i;
  gen_perm(perm,k3);
  for(i=0;i<k3;i++) {
    j=perm[i];
    if(locilist[j]->type==ST_MARKER) {
      (void)peel_locus(locilist,j,k3,1,loki);
    }
  }
#ifndef NDEBUG
  if((loki->params.debug_level)&4) {
    (void)fputc('*',stdout);
    (void)fflush(stdout);
  }
#endif
}

double calc_tl_like(struct Locus *loc,const int sflag,struct loki *loki)
{
  int i,k,link;
  double l;
	
  link=loc->link_group;
  if(link<0) {
    locilist[0]=loc;
    i=0;
    k=1;
  } else {
    get_locuslist(locilist,link,&k,0);
    gnu_qsort(locilist,(size_t)k,sizeof(void *),cmp_loci);
    for(i=0;i<k;i++) if(locilist[i]==loc) break;
  }
  l=peel_locus(locilist,i,k,sflag,loki);
  return l;
}

static void TL_Free(void)
{
  if(perm) free(perm);
  if(locilist) free(locilist);
  if(new_freq) free(new_freq);
}

void TL_Alloc(const struct loki *loki)
{
  int k;
	
  k=loki->markers->n_markers+loki->params.max_tloci;
  if(!k) k++;
  perm=lk_malloc(k*2*sizeof(int));
  new_freq=lk_malloc(2*(loki->pedigree->n_genetic_groups+loki->markers->n_links+1)*sizeof(double));
  prior=new_freq+loki->pedigree->n_genetic_groups*2;
  locilist=lk_malloc(sizeof(void *)*k);
  if(atexit(TL_Free)) message(WARN_MSG,"Unable to register exit function TL_Free()\n");
	
}

void Flip_TL_Alleles(const int tl,struct loki *loki)
{
  double z,z1,*p;
  int i;
  struct Locus *loc;
	
  loc=loki->models->tlocus+tl;
  if(loc->flag&LOCUS_SAMPLED) {
    Adjust_for_TL(loc,1.0,loki);
    loc->flag&=~LOCUS_SAMPLED;
  }
  p=loc->eff[0];
  z=p[0];
  z1=p[1];
  Adjust_Mean(-z1,loki);
  p[1]=-z1;
  p[0]=z-z1;
  for(i=0;i<loki->pedigree->n_genetic_groups;i++) {
    p=loc->freq[i];
    p[0]=1.0-p[0];
    p[1]=1.0-p[1];
  }
}

void Flip_TL_Mode(const int tl,struct loki *loki)
{
  double z,z1,*eff,p,q,va,vd,a,d,lk[4],aa[4],dd[4],max;
  int i,ng;
  struct Locus *loc;

  loc=loki->models->tlocus+tl;
  if(loc->flag&LOCUS_SAMPLED) {
    Adjust_for_TL(loc,1.0,loki);
    loc->flag&=~LOCUS_SAMPLED;
  }
  ng=loki->pedigree->n_genetic_groups;
  if(ng>1) {
    i=(int)(safe_ranf()*(double)ng);
  } else i=0;
  /* Get existing additive and dominance variances */
  eff=loc->eff[0];
  a=eff[1]*.5;
  d=eff[0]-a;
  p=loc->freq[i][0];
  q=1.0-p;
  z=a+(q-p)*d;
  va=2.0*p*q*z*z;
  z=2.0*p*q*d;
  vd=z*z;
  max=-DBL_MAX;
  for(i=0;i<4;i++) {
    d=sqrt(vd)/(2.0*p*q);
    if(i&2) d=-d;
    z=(i&1)?-1.0:1.0;
    a=z*sqrt(va/(2.0*p*q))-d*(q-p);
    eff[0]=a+d;
    eff[1]=2.0*a;
    z1=calc_tl_like(loc,0,loki);
    lk[i]=z1;
    aa[i]=a;
    dd[i]=d;
    if(z1>max) max=z1;
  }
  z=0.0;
  for(i=0;i<4;i++) {
    z+=exp(lk[i]-max);
    lk[i]=z;
  }
  z1=safe_ranf()*z;
  for(i=0;i<4;i++) {
    if(z1<=lk[i]) break;
  }
  eff[0]=aa[i]+dd[i];
  eff[1]=2.0*aa[i];
}

/* Sample a new position for trait locus.  Allows moves to be made between
 * linkage groups (including to and from unlinked).
 * BIGMOVE_PROB is the probability of proposing a large move (uniform over entire genome)
 * vs. a small move (current location +/- 1 interval)
 * If unlinked then a move to linked is always attempted.
 */
void Sample_TL_Position(const int tl,struct loki *loki)
{
  int i,j,j2,k,k1,k2,fg,link,oldlink,step,n_links;
  double l,l1,old_pos[2],map_length,z,z1,z2,r,*prob,ttm,p_i[3],p_i1[3],s1,s2;
  struct Locus *loc;
  struct Link *linkage;
	
  /* In the case of no linkage groups,
     just sample genotypes for trait locus and return */
  n_links=loki->markers->n_links;
  loc=loki->models->tlocus+tl;
  if(!n_links) {
    (void)calc_tl_like(loc,1,loki);
    return;
  }
  linkage=loki->markers->linkage;
  prob=prior+n_links+1;
  /* Set up prior probs. for chromosomes (and unlinked region) */
  /* Note that we use the average of sex-specific maps */
  for(i=0;i<=n_links;i++) prior[i]=0.0;
  ttm=0.0;
  for(k=0;k<=loki->markers->sex_map;k++)	{
    z=loki->markers->total_maplength[k];
    ttm+=z;
    for(i=0;i<n_links;i++) {
      map_length=linkage[i].r2[k]-linkage[i].r1[k];
      z-=map_length;
      prior[i]+=map_length;
    }
    prior[i]+=z;
  }
  if(loc->flag&TL_LINKED) {
    link=loc->link_group;
    old_pos[0]=loc->pos[0];
    old_pos[1]=loc->pos[1];
  } else {
    link= -1;
    old_pos[0]=old_pos[1]=0.0;
  }
  l=calc_tl_like(loc,0,loki);
  assert(l!=-DBL_MAX);
  oldlink=link;
  z1=1.0;
  /* If unlinked always attempt a 'big move', otherwise do so with probability BIGMOVE_PROB  */
  /* A 'big' move can be to anywhere in the genome (except that if unlinked can not jump to */
  /* another unlinked location) */
  if(link== -1 || (BIGMOVE_PROB>0.0 && ranf()<=BIGMOVE_PROB))	{
    step=5;
    z=0.0;
    for(i=0;i<=n_links;i++)	{
      z+=prior[i];
      prob[i]=z;
    }
    if(link== -1) { /* If already unlinked, prevent choosing unlinked again */
      z-=prior[n_links];
      prob[n_links]=z;
      j=n_links;
    } else j=n_links+1;
    assert(z>0.0);
    do {
      r=z*safe_ranf();
      for(i=0;i<j;i++) if(prior[i]>0.0 && r<=prob[i]) break;
    } while(i==j);
    link=i;
    if(link==n_links)	{
      link= -1;
      loc->pos[0]=loc->pos[1]=0.0;
    } else {
      if(link) r-=prob[link-1]; /* Get position within the chromosome we've landed on relative to left of linkage map */
      /* Get list of loci on linkage group (excluding current locus) */
      fg=loc->flag;
      loc->flag&=~TL_LINKED;
      get_locuslist(locilist,link,&k,0);
      gnu_qsort(locilist,(size_t)k,(size_t)sizeof(void *),cmp_loci);
      loc->flag=fg;
      if(loki->markers->sex_map)	{ /* Find interval */
	for(j=0;j<k;j++) {
	  z=locilist[j]->pos[0]-linkage[link].r1[0];
	  z+=locilist[j]->pos[1]-linkage[link].r1[1];
	  if(z>=r) break;
	}
	/* Sample male and female positions (independently) within chosen interval */
	for(k2=0;k2<2;k2++) {
	  if(j<k) {
	    k1=perm[j];
	    z=locilist[j]->pos[k2];
	  } else z=linkage[link].r2[k2];
	  if(j)	{
	    z2=locilist[j-1]->pos[k2];
	  } else z2=linkage[link].r1[k2];
	  loc->pos[k2]=z2+(z-z2)*safe_ranf();
	}
      } else loc->pos[0]=loc->pos[1]=r+linkage[link].r1[0];
    }
    loc->link_group=link;
    if(link!=oldlink)	{
      if(link<0 && oldlink>=0) z1=ttm/(BIGMOVE_PROB*(ttm-prior[n_links]));
      else if(link>=0 && oldlink<0) z1=BIGMOVE_PROB*(ttm-prior[n_links])/ttm;
    }
    if(link<0) {
      loc->flag&=~TL_LINKED;
      loc->flag|=TL_UNLINKED;
    } else {
      loc->flag&=~TL_UNLINKED;
      loc->flag|=TL_LINKED;
    }
  } else { /* 'Small' move - move at most 1 interval from current location */
    step=4;
    get_locuslist(locilist,link,&k,0);
    gnu_qsort(locilist,(size_t)k,(size_t)sizeof(int),cmp_loci);
    for(j=0;j<k;j++) if(locilist[j]==loc) break;
    assert(j<k);
    for(j2=j+1;j2<k;j2++) locilist[j2-1]=locilist[j2];
    k--;
    s1=check_intervals(j,k,link,locilist,p_i,SMALLMOVE_P,loki);
    /* Pick an interval */
    do {
      z2=ranf();
      z=0.0;
      for(k2=0;k2<3;k2++) if(p_i[k2]>0.0)	{
	z+=p_i[k2];
	if(z2<=z) break;
      }
    } while(k2==3);
    /* Have we changed intervals? */
    if(k2!=1) {
      j2=j+k2-1;
      /* Compute probs. for new interval */
      s2=check_intervals(j2,k,link,locilist,p_i1,SMALLMOVE_P,loki);
      /* Compute proposal probability */
      z1=(s2/s1)*p_i1[2-k2]/p_i[k2];
    } else j2=j;
    /* Sample new positions */
    for(k2=0;k2<=loki->markers->sex_map;k2++)	{
      if(j2) z=locilist[j2-1]->pos[k2];
      else z=linkage[link].r1[k2];
      if(j2<k) z2=locilist[j2]->pos[k2];
      else z2=linkage[link].r2[k2];
      loc->pos[k2]=z+safe_ranf()*(z2-z);
    } 
    if(k2==1) loc->pos[1]=loc->pos[0];
  }
#ifndef NDEBUG
  if((loki->params.debug_level)&4) (void)printf("[%d:%d]",step,tl);
#endif
  loki->sys.move_stats[step].n++;
  l1=calc_tl_like(loc,0,loki);
  if(l1== -DBL_MAX) z= -1.0;
  else z=safe_exp(l1-l)*z1;
  i=0;
  if(ranf()>=z) {
    i=1;
    loc->pos[0]=old_pos[0];
    loc->pos[1]=old_pos[1];
    link=oldlink;
    loc->link_group=link;
    if(link<0) {
      loc->flag&=~TL_LINKED;
      loc->flag|=TL_UNLINKED;
    } else {
      loc->flag&=~TL_UNLINKED;
      loc->flag|=TL_LINKED;
    }
#ifndef NDEBUG
    if((loki->params.debug_level)&4) (void)fputc('F',stdout);
#endif
  } else {	
#ifdef NDEBUG
    if((loki->params.debug_level)&4) (void)fputc('S',stdout);
#endif
    loki->sys.move_stats[step].success++;
  }
  /* Always sample genotypes before returning */
#ifdef NDEBUG
  z=calc_tl_like(loc,1,loki);
  assert(z!=-DBL_MAX);
  if((loki->params.debug_level)&4) (void)fflush(stdout);
#else
  (void)calc_tl_like(loc,1,loki);
#endif
}

int get_tl_position(double *pos,const struct loki *loki)
{
  int i,j,k,k2,link,n_links;
  double z,z1,z2,map_length;
  struct Link *linkage;
	
  if(!loki->markers->n_markers) {
    pos[0]=pos[1]=0;
    return -1;
  }
  n_links=loki->markers->n_links;
  linkage=loki->markers->linkage;
  for(i=0;i<=n_links;i++) prior[i]=0.0;
  z1=z=0.0;
  for(k=0;k<=loki->markers->sex_map;k++) {
    z=loki->markers->total_maplength[k];
    z1+=z;
    for(i=0;i<n_links;i++) {
      map_length=linkage[i].r2[k]-linkage[i].r1[k];
      z-=map_length;
      prior[i]+=map_length;
    }
  }
  prior[i]+=z;
  do {
    z2=z1*safe_ranf();
    z=0.0;
    for(i=0;i<=n_links;i++) {
      if(prior[i]>0.0) {
	z+=prior[i];
	if(z2<=z) {
	  z1=z2+prior[i]-z;
	  break;
	}
      }
    }
  } while(i==n_links+1);
  link=(i==n_links?-1:i);
  if(link<0) pos[0]=pos[1]=0.0;
  else if(loki->markers->sex_map) {
    /* Get list of loci on linkage group */
    get_locuslist(locilist,link,&k,0);
    gnu_qsort(locilist,(size_t)k,(size_t)sizeof(void *),cmp_loci);
    /* Find interval */
    for(j=0;j<k;j++) {
      z=locilist[j]->pos[0]=linkage[link].r1[0];
      z+=locilist[j]->pos[1]=linkage[link].r1[1];
      if(z>=z1) break;
    }
    /* Sample male and female positions (independently) within chosen interval */
    for(k2=0;k2<2;k2++) {
      if(j<k) {
	z=locilist[j]->pos[k2];
      } else z=linkage[link].r2[k2];
      if(j) {
	z2=locilist[j-1]->pos[k2];
      } else z2=linkage[link].r1[k2];
      pos[k2]=z2+(z-z2)*safe_ranf();
    }
  } else pos[0]=pos[1]=z1+linkage[link].r1[0];
  return link;
}

/* Trait locus Birth/Death */
void TL_Birth_Death(struct loki *loki)
{
  int i,j,x=-1,step,nq,er=0;
  double l,l1,r,z,z1,u1,u2,u3,d,a,q1;
  double va_prop,vd_prop,alpha= -1,pp[2],old_res_var;
  double newpos[2],(*res_fn)(struct loki *),res;
  double old_res,mean_eff;
  int newlink,cens_flag,mtype;
  struct Locus *loc=0;
#ifndef NDEBUG
  double old_l;
#endif
	
  /* Count how many QTL currently in models[0] (nq) and no. unused QTL (j) */
  for(j=nq=x=0;x<loki->params.n_tloci;x++) if(!loki->models->tlocus[x].flag) j++; else nq++;
  j+=loki->params.max_tloci-loki->params.n_tloci;
  if(!(j || nq)) return;
  pp[0]=j?BIRTH_STEP:0.0; /* Birth step possible if there is space for new QTL */
  pp[1]=nq>loki->params.min_tloci?DEATH_STEP:0.0; /* Death step possible if no. QTL not at minimum */
  for(z=0.0,i=0;i<2;i++) z+=pp[i];
  /* We have a fixed no. loci - can't change anything */
  if(z<=0.0) return;
  step=(ranf()*z<=pp[0])?0:1;
  r=pp[step]/z; /* Proposal probability for step */
  if(!step) { /* Get position for new TL */
    newlink=get_tl_position(newpos,loki);
  } else {
    newlink=0;
    newpos[0]=newpos[1]=0.0;
  }
  res=loki->models->residual_var[0];
  va_prop=res*PROP_RATIO;
  vd_prop=res*PROP_RATIO;
  /* Find proposal distribution for next time if current step was to be accepted */
  if(step) { /* Death steps reduce QTL numbers by 1 */
    pp[0]=BIRTH_STEP;
    pp[1]=((nq-1)>loki->params.min_tloci)?DEATH_STEP:0.0;
  } else { /* Birth steps increase QTL numbers by 1 */
    pp[0]=(j-1)?BIRTH_STEP:0.0;
    pp[1]=DEATH_STEP;
  }
  for(z=0.0,i=0;i<2;i++) z+=pp[i];
#ifndef NDEBUG
  if((loki->params.debug_level)&4) (void)printf("[%d]",step);
#endif
  loki->sys.move_stats[step].n++;
  mtype=loki->models->models[0].var.type;
  if(!loki->models->censor_mode && (mtype&ST_CENSORED)) {
    cens_flag=1;
    res_fn=&Calc_CensResLike;
  } else {
    cens_flag=0;
    res_fn=&Calc_ResLike;
  }
  old_res=res_fn(loki);
  assert(old_res!=-DBL_MAX);
  /* If censoring and integrating over censored values, the sampled censored
   * values will be invalid here, and will need to be sampled */
  if(cens_flag) Sample_Censored(loki);
  old_res_var=res;
  /* If censoring and integrating over censored values, the sampled censored
   * values will be invalid here, and will need to be sampled */
  if(cens_flag) Sample_Censored(loki);
  if(!step) { /* Birth Step */
    r=pp[1]/(z*r); /* Proposal ratio q(death)/q(birth) */
#ifndef NDEBUG
    if((loki->params.debug_level)&4) (void)printf("(r=%g)",r);
#endif
    /* Factor in Poisson prior on number of loci */
    if(loki->models->tloci_mean_set) r*=loki->models->tloci_mean/(double)(1+nq);
    u1=genexp(va_prop);
    u2=genexp(vd_prop);
    for(u3=0.0,i=0;i<loki->pedigree->n_genetic_groups;i++) u3+=(new_freq[i]=ranf());
    u3/=(double)loki->pedigree->n_genetic_groups;
    d=sqrt(u2)/(2.0*u3*(1.0-u3));
    if(ranf()<0.5) d= -d;
    z=(ranf()<0.5)?-1.0:1.0;
    a=z*sqrt(u1/(2.0*u3*(1.0-u3)))-d*(1.0-2.0*u3);
    if(loki->models->no_overdominant && fabs(d)>fabs(a)) {
#ifndef NDEBUG
      if((loki->params.debug_level)&4) {
	(void)fputc('f',stdout);
	(void)fflush(stdout);
      }
#endif
      return;
    }
    q1=Calc_Resprop(loki);
    /* Find next available unused QTL */
    x=get_new_traitlocus(2,loki);
    if(x<0) ABT_FUNC("Internal error - couldn't get new trait locus\n");
    loc=loki->models->tlocus+x;
    loc->model_flag=1;
    loc->pos[0]=newpos[0];
    loc->pos[1]=newpos[1];
    loc->link_group=newlink;
    loc->flag=(newlink<0?TL_UNLINKED:TL_LINKED);
    /* Get likelihood for QTL not in models[0] */
    l=old_res;
    for(i=0;i<loki->pedigree->n_genetic_groups;i++) {
      loc->freq[i][0]=new_freq[i];
      loc->freq[i][1]=1.0-new_freq[i];
    }
    loc->eff[0][0]=a+d;
    loc->eff[0][1]=a*2.0;
    mean_eff=(1.0-u3)*(1.0-u3)*loc->eff[0][1]+2.0*u3*(1.0-u3)*loc->eff[0][0];
    Adjust_Mean(mean_eff,loki);
    /* Calculate likelihood (and sample) for models[0] with QTL (and mu = mu - mean_eff) */
    l1=calc_tl_like(loc,2,loki);
    if(l1== -DBL_MAX) er=1;
    else {
      /* We'll need to sample more censored values conditional on the new QTL */
      if(cens_flag) Sample_Censored(loki);
      z=Sample_ResVar(loki);
      res=loki->models->residual_var[0];
      q1=q1-z+Calc_Res_Ratio(res,old_res_var,loki)+Calc_Var_Prior(res,loki)-Calc_Var_Prior(old_res_var,loki);
      /* prior prob. for TL effects */
      z1=0.0;
      for(i=0;i<2;i++) z1+=loc->eff[0][i]*loc->eff[0][i];
      z=-.5*(2.0*log(2.0*M_PI*loki->models->tau[0])+z1/loki->models->tau[0]);
      /* proposal prob. for u1,u2 */
      z1= -u1/va_prop-u2/vd_prop-log(va_prop*vd_prop);
      /* det(jacobian) for transformation from (u1,u2,u3)->(effect[0],effect[1],p) */
      z1+=.5*(log(2.0*u1*u2)+3.0*(log(u3)+log(1.0-u3)));
      /* Acceptance ratio */
      alpha=safe_exp(l1+z-l-z1+q1);
#ifndef NDEBUG
      if((loki->params.debug_level)&8) {(void)printf("<%g,%g,%g,%g,%g>",l,l1,z,z1,q1);}
#endif
      if(alpha<DBL_MAX) alpha*=r;
    }
    if(!er) z=ranf();
#ifndef NDEBUG
    if((loki->params.debug_level)&8) {
      if(er) (void)fputs("(er)",stdout);
      else (void)printf("(%g,%g)",alpha,z);
    }
#endif
    if(!er && z<alpha)	{
      /* If successful, sample genotypes for new QTL */
#ifndef NDEBUG
      if((loki->params.debug_level)&4) (void)fputc('S',stdout);
#endif
      loki->sys.move_stats[step].success++;
    } else {
      loki->models->residual_var[0]=old_res_var;
      Adjust_for_TL(loc,1.0,loki);
      if(loc->flag&LOCUS_SAMPLED) delete_traitlocus(loc);
      loc->flag=0;
      Adjust_Mean(-mean_eff,loki);
#ifndef NDEBUG		
      if((loki->params.debug_level)&1)	{
	l=res_fn(loki);
	z=fabs(l-old_res);
	assert(z<=1.0e-8);
      }
      if((loki->params.debug_level)&4) (void)fputc('F',stdout);
#endif
    }
  } else if(step==1) { /* Death_Step */
    r=pp[0]/(z*r); /* Proposal ratio q(birth)/q(death) */
#ifndef NDEBUG
    if((loki->params.debug_level)&4) (void)printf("(r=%g)",r);
#endif
    /* Factor in Poisson prior on number of loci */
    if(loki->models->tloci_mean_set) r*=(double)nq/loki->models->tloci_mean;
    /* Pick a non-blank QTL at random */
    i=(int)(safe_ranf()*(double)nq);
    for(x=0;x<loki->params.n_tloci;x++) if(loki->models->tlocus[x].flag) if(!(i--)) break;
    loc=loki->models->tlocus+x;
#ifndef NDEBUG
    if((loki->params.debug_level)&1)	{
      old_l=calc_tl_like(loc,0,loki);
      assert(old_l!= -DBL_MAX);
    } else old_l=0.0;
#endif
    q1=Calc_Resprop(loki);
    for(u3=0.0,i=0;i<loki->pedigree->n_genetic_groups;i++) u3+=loc->freq[i][0];
    u3/=(double)loki->pedigree->n_genetic_groups;
    mean_eff=(1.0-u3)*(1.0-u3)*loc->eff[0][1]+2.0*u3*(1.0-u3)*loc->eff[0][0];
    /* Adjust residuals as if taking QTL out of models[0] */
    Adjust_Mean(-mean_eff,loki);
    Adjust_for_TL(loc,1.0,loki);
    /* Get new variance estimate */
    q1-=Sample_ResVar(loki);
    res=loki->models->residual_var[0];
    /* Put QTL back */
    Adjust_Mean(mean_eff,loki);
    Adjust_for_TL(loc,-1.0,loki);
    /* And calculate likelihood with new variance estimate */
    l=calc_tl_like(loc,0,loki);
    q1+=Calc_Res_Ratio(res,old_res_var,loki)+Calc_Var_Prior(res,loki)-Calc_Var_Prior(old_res_var,loki);
    /* and take means out again! */
    Adjust_Mean(-mean_eff,loki);
    Adjust_for_TL(loc,1.0,loki);
    a=loc->eff[0][1]*.5;
    d=loc->eff[0][0]-a;
    z=a+d*(1.0-2.0*u3);
    u1=2.0*u3*(1.0-u3)*z*z;
    z=2.0*u3*(1.0-u3)*d;
    u2=z*z;
    /* Calculate likelihood if QTL is out of models[0] */
    l1=res_fn(loki);
    if(l1== -DBL_MAX) alpha= -1.0;
    else {
      /* Prior for QTL effects */
      z1=0.0;
      for(i=0;i<2;i++) z1+=loc->eff[0][i]*loc->eff[0][i];
      z=-.5*(2.0*log(2.0*M_PI*loki->models->tau[0])+z1/loki->models->tau[0]);
      /* proposal prob. for u1,u2 */
      z1= -u1/va_prop-u2/vd_prop-log(va_prop*vd_prop);
      /* det(jacobian) for transformation from (u1,u2,u3)->(effect[0],effect[1],p) */
      z1+=.5*(log(2.0*u1*u2)+3.0*(log(u3)+log(1.0-u3)));
      /* Acceptance ratio */
      alpha=safe_exp(l1-l-z+z1+q1);
#ifndef NDEBUG
      if((loki->params.debug_level)&8) {(void)printf("<%g,%g,%g,%g,%g - %g>",l,l1,z,z1,q1,l1-l-z+z1+q1);}
#endif
      if(alpha<DBL_MAX) alpha*=r;
    }
    z=ranf();
#ifndef NDEBUG
    if((loki->params.debug_level)&8) (void)printf("(%g,%g)",alpha,z);
#endif
    if(z<alpha) {
      delete_traitlocus(loc);
#ifndef NDEBUG
      if((loki->params.debug_level)&4) (void)fputc('S',stdout);
#endif
      loki->sys.move_stats[step].success++;
    } else {
      loki->models->residual_var[0]=old_res_var;
      Adjust_Mean(mean_eff,loki);
      Adjust_for_TL(loc,-1.0,loki);
#ifndef NDEBUG
      if((loki->params.debug_level)&1)	{
	l=res_fn(loki);
	z=fabs(l-old_res);
	assert(z<1.0e-8);
	l=calc_tl_like(loc,0,loki);
	z=fabs(l-old_l);
	assert(z<1.0e-8);
      }
      if((loki->params.debug_level)&4) (void)fputc('F',stdout);
#endif
    }
  }
}
