/****************************************************************************
 *                                                                          *
 *     Loki - Programs for genetic analysis of complex traits using MCMC    *
 *                                                                          *
 *             Simon Heath - Rockefeller University                         *
 *                                                                          *
 *                       February 1998                                      *
 *                                                                          *
 * loki_ibd.c:                                                              *
 *                                                                          *
 * Routines for estimating pairwise IBD matrices                            *
 *                                                                          *
 * Copyright (C) Simon C. Heath 1997, 2000, 2002                            *
 * This is free software.  You can distribute it and/or modify it           *
 * under the terms of the Modified BSD license, see the file COPYING        *
 *                                                                          *
 ****************************************************************************/

#include <config.h>
#include <stdlib.h>
#include <string.h>
#ifdef USE_DMALLOC
# include <dmalloc.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <math.h>
#include <stdio.h>
#include <float.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>

#include "ranlib.h"
#include "utils.h"
#include "string_utils.h"
#include "libhdr.h"
#include "loki.h"
#include "loki_peel.h"
#include "loki_utils.h"
#include "loki_compress.h"
#include "loki_ibd.h"
#include "lk_malloc.h"
#include "seg_pen.h"

static double *pos_list[2];
static int *loci,*seg[2],*n_longs,*n_pairs,*inbr,**inb_sparse,**inb_sparse1,n_cmp,***ibd[2];
static struct Locus **locilist;
static unsigned long *founders;
static char *suff[]={".gz",".bz2",".zip",".Z"};

void get_founder_params(unsigned long **fnd,int **nl,int **np,int **in,const struct loki *loki)
{
	if(!founders) get_founders(&founders,&inbr,&n_longs,&n_pairs,loki);
	if(fnd) *fnd=founders;
	if(in) *in=inbr;
	if(np) *np=n_pairs;
	if(nl) *nl=n_longs;
}

static void free_IBD(void)
{
	int i;
	
	if(ibd[0]) {
		if(ibd[0][0]) {
			if(ibd[0][0][0]) free(ibd[0][0][0]);
			free(ibd[0][0]);
		}
		free(ibd[0]);
	}
	if(loci) free(loci);
	if(locilist) free(locilist);
	if(seg[0]) free(seg[0]);
	if(founders) free(founders);
	if(n_longs) free(n_longs);
	if(pos_list[0]) free(pos_list[0]);
	if(inb_sparse) {
		for(i=0;i<n_cmp;i++) if(inb_sparse[i]) free(inb_sparse[i]);
		free(inb_sparse);
	}
}

void sample_segs(const struct loki *loki) 
{
	int i,i1,j,k,comp,link,nloci,nkids,par_flag;
	int s,s1,kid,cs;
	double *p[2],pp[2],z,*recom[2],x,x1,*r,z1;
	struct Id_Record *id_array,**kids;
	
	k=loki->markers->n_markers+loki->params.n_tloci;
	id_array=loki->pedigree->id_array;
	if(!k) return;
	p[0]=lk_malloc(sizeof(double)*k*4);
	p[1]=p[0]+k;
	recom[0]=p[1]+k;
	recom[1]=recom[0]+k;
	for(i1=comp=0;comp<loki->pedigree->n_comp;comp++) {
		cs=loki->pedigree->comp_size[comp];
		if(cs>1) {
			for(link=0;link<loki->markers->n_links;link++) {
				get_locuslist(locilist,link,&nloci,0);
				if(!nloci) continue;
				if(nloci>1) gnu_qsort(locilist,(size_t)nloci,sizeof(void *),cmp_loci);
				for(par_flag=0;par_flag<2;par_flag++) {
					x=locilist[0]->pos[par_flag];
					for(k=1;k<nloci;k++) {
						x1=locilist[k]->pos[par_flag];
						recom[par_flag][k-1]=.5*(1.0-exp(0.02*(x-x1)));
						x=x1;
					}
				}
				for(i=i1;i<i1+cs;i++) {
					if(id_array[i].nkids) {
						par_flag=2-id_array[i].sex;
						r=recom[par_flag];
						nkids=id_array[i].nkids;
						kids=id_array[i].kids;
						for(j=0;j<nkids;j++) {
							kid=kids[j]->idx;
							s=locilist[0]->seg[par_flag][kid];
							if(s<0) {
								p[0][0]=p[1][0]=.5;
							} else {
								p[s][0]=1.0;
								p[s^1][0]=0.0;
							}
							for(k=1;k<nloci;k++) {
								s=locilist[k]->seg[par_flag][kid];
								if(s<0) {
									pp[0]=.5*(p[0][k-1]*(1.0-r[k-1])+p[1][k-1]*r[k-1]);
									pp[1]=.5*(p[1][k-1]*(1.0-r[k-1])+p[0][k-1]*r[k-1]);
									z=pp[0]+pp[1];
									p[0][k]=pp[0]/z;
									p[1][k]=pp[1]/z;
								} else {
									p[s][k]=1.0;
									p[s^1][k]=0.0;
								}
							}
							if(s<0) {
								s=(ranf()<p[0][nloci-1])?0:1;
								locilist[nloci-1]->seg[par_flag][kid]=s;
							}
							for(k=nloci-2;k>=0;k--) {
								s1=locilist[k]->seg[par_flag][kid];
								if(s1<0) {
									pp[s]=p[s][k]*(1.0-r[k]);
									pp[s^1]=p[s^1][k]*r[k];
									z=pp[0]+pp[1];
									z1=ranf();
									s1=(z*z1<pp[0])?0:1;
									locilist[k]->seg[par_flag][kid]=s1;
								}
								s=s1;
							}
						}
					}
				}
			}
			for(k=0;k<loki->params.n_tloci;k++) {
				if(loki->models->tlocus[k].flag&TL_UNLINKED) {
					locilist[0]=loki->models->tlocus+k;
					for(i=i1;i<i1+cs;i++) {
						if(id_array[i].sire) {
							for(par_flag=0;par_flag<2;par_flag++) {
								if(locilist[0]->seg[par_flag][i]<0) {
									s=ranf()<.5?0:1;
									locilist[0]->seg[par_flag][i]=ranf()<.5?0:1;
								} 
							}
						}
					}
				}
			}
		}
		i1+=cs;
	}
	free(p[0]);
	return;
}

int SetupIBD(const struct loki *loki)
{
	int i,i1,i2,i3,j,k,k1,k2,np,cs,n_long,*sz,*sz1;
	int ids,idd,ct,ct1,*pp,*pp1;
	unsigned long *tpl,*tpl1,*tpl2;
	
	if(!founders) get_founders(&founders,&inbr,&n_longs,&n_pairs,loki);
	message(INFO_MSG,"Setting up IBD handling\n");
	for(i=j=k1=0;i<loki->markers->n_links;i++) {
		if(loki->markers->linkage[i].ibd_est_type) {
			k=0;
			switch(loki->markers->linkage[i].ibd_est_type) {
			 case IBD_EST_DISCRETE:
				k=loki->markers->linkage[i].ibd_list->idx;
				break;
			 case IBD_EST_MARKERS:
				k=loki->markers->linkage[i].n_markers;
				break;
			 case IBD_EST_GRID:
				k=1+(int)(.5+(loki->markers->linkage[i].ibd_list->pos[1]-loki->markers->linkage[i].ibd_list->pos[0])/loki->markers->linkage[i].ibd_list->pos[2]);
				break;
			}
			if(k>k1) k1=k;
			j+=k;
		}
	}
	/* Allocate memory for j triangular matrices */
	if(j) {
		pos_list[0]=lk_malloc(sizeof(double)*k1*2);
		pos_list[1]=pos_list[0]+k1;
		for(np=i=0;i<loki->pedigree->n_comp;i++) np+=n_pairs[i];
		if(np) {
			ibd[0]=lk_malloc(sizeof(void *)*j*2);
			ibd[1]=ibd[0]+j;
			ibd[0][0]=lk_malloc(sizeof(void *)*2*j*loki->pedigree->n_comp);
			ibd[1][0]=ibd[0][0]+j*loki->pedigree->n_comp;
			ibd[0][0][0]=lk_calloc((size_t)2*j*np,sizeof(int));
			ibd[1][0][0]=ibd[0][0][0]+j*np;
			for(i=1;i<j;i++) {
				for(k1=0;k1<2;k1++) {
					ibd[k1][i]=ibd[k1][i-1]+loki->pedigree->n_comp;
					ibd[k1][i][0]=ibd[k1][i-1][0]+np;
				}
			}
			for(i=1;i<loki->pedigree->n_comp;i++) {
				k1=n_pairs[i-1];
				for(k=0;k<j;k++) {
					for(k2=0;k2<2;k2++) ibd[k2][k][i]=ibd[k2][k][i-1]+k1;
				}
			}
		}
	}
	k=loki->markers->n_markers+loki->params.max_tloci;
	loci=lk_malloc((k+2*loki->pedigree->ped_size)*sizeof(int));
	locilist=k?lk_malloc(sizeof(void *)*k):0;
	seg[0]=lk_malloc(sizeof(int)*2*loki->pedigree->ped_size);
	seg[1]=seg[0]+loki->pedigree->ped_size;
	for(i1=i=0;i<loki->pedigree->n_comp;i++) if(loki->pedigree->comp_size[i]>i1) i1=loki->pedigree->comp_size[i];
	sz=lk_malloc(sizeof(int)*i1*2);
	sz1=sz+i1;
	inb_sparse=lk_malloc(sizeof(void *)*2*loki->pedigree->n_comp);
	inb_sparse1=inb_sparse+loki->pedigree->n_comp;
	for(i=0;i<2*loki->pedigree->n_comp;i++) inb_sparse[i]=0;
	tpl=founders;
	for(i1=i=0;i<loki->pedigree->n_comp;i++) {
		if(!loki->pedigree->singleton_flag[i]) {
			n_long=n_longs[i];
			cs=loki->pedigree->comp_size[i];
			tpl2=tpl;
			i3=i1;
			for(k1=k=0;k<cs;k++,i1++,tpl+=n_long) {
				sz[k]=sz1[k]=0;
				tpl1=tpl2;
				ids=loki->pedigree->id_array[i1].sire-1;
				idd=loki->pedigree->id_array[i1].dam-1;
				for(i2=loki->pedigree->comp_start[i];i2<i1;i2++,tpl1+=n_long) {
					for(k2=0;k2<n_long;k2++) if(tpl[k2]&tpl1[k2]) break;
					if(k2<n_long) {
						sz1[k]++;
						if((inbr[i1]||inbr[i2])||(ids!=i2&&idd!=i2)) sz[k]++;
					}
				}
			}
			k1=k2=cs+1;
			for(k=0;k<cs;k++) {
				k1+=sz[k];
				k2+=sz1[k];
			}
			inb_sparse[i]=lk_malloc(sizeof(int)*(k1+k2));
			inb_sparse1[i]=inb_sparse[i]+k1;
			i1=i3;
			tpl=tpl2;
			ct=ct1=cs+1;
			pp=inb_sparse[i];
			pp1=inb_sparse1[i];
			for(k1=k=0;k<cs;k++,i1++,tpl+=n_long) {
				pp[k]=ct;
				pp1[k]=ct1;
				tpl1=tpl2;
				ids=loki->pedigree->id_array[i1].sire-1;
				idd=loki->pedigree->id_array[i1].dam-1;
				for(i2=loki->pedigree->comp_start[i];i2<i1;i2++,tpl1+=n_long) {
					for(k2=0;k2<n_long;k2++) if(tpl[k2]&tpl1[k2]) break;
					if(k2<n_long) {
						pp1[ct1++]=i2; 
						if((inbr[i1]||inbr[i2])||(ids!=i2&&idd!=i2)) pp[ct++]=i2;
					}
				}
			}
			pp[k]=ct;
			pp1[k]=ct1;
		}
	}
	free(sz);
	n_cmp=loki->pedigree->n_comp;
	if(atexit(free_IBD)) message(WARN_MSG,"Unable to register exit function free_IBD()\n");	
	return j;
}

static void Set_Trans(double *pos,struct Locus **perm,int idx,int n_loci,int comp,const struct loki *loki)
{
	int i,i1,k,k2,ids,idd,cs;
	double theta,Mtp[2],Ptp[2],z,z1;
	struct Id_Record *id_array;
	struct Locus *loc;
	
	cs=loki->pedigree->comp_size[comp];
	i1=loki->pedigree->comp_start[comp];
	id_array=loki->pedigree->id_array;
	for(i=i1;i<i1+cs;i++) {
		ids=id_array[i].sire;
		idd=id_array[i].dam;
		if(ids || idd) {
			Mtp[0]=Mtp[1]=Ptp[0]=Ptp[1]=1.0;
			for(k=idx-1;k>=0;k--) {
				loc=perm[k];
				if(!(loc->flag&LOCUS_SAMPLED)) continue;
				k2=loc->seg[X_MAT][i];
				if(k2<0) continue;
				theta=.5*(1.0-exp(-0.02*(pos[X_MAT]-loc->pos[X_MAT])));
				Mtp[k2]=1.0-theta;
				Mtp[1-k2]=theta;
				break;
			}
			for(k=idx-1;k>=0;k--) {
				loc=perm[k];
				if(!(loc->flag&LOCUS_SAMPLED)) continue;
				k2=loc->seg[X_PAT][i];
				if(k2<0) continue;
				theta=.5*(1.0-exp(-0.02*(pos[X_PAT]-loc->pos[X_PAT])));
				Ptp[k2]=1.0-theta;
				Ptp[1-k2]=theta;
				break;
			}
			for(k=idx;k<n_loci;k++) {
				loc=perm[k];
				if(!(loc->flag&LOCUS_SAMPLED)) continue;
				k2=loc->seg[X_MAT][i];
				if(k2<0) continue;
				theta=.5*(1.0-exp(-0.02*(loc->pos[X_MAT]-pos[X_MAT])));
				Mtp[k2]*=1.0-theta;
				Mtp[1-k2]*=theta;
				break;
			}
			for(k=idx;k<n_loci;k++) {
				loc=perm[k];
				if(!(loc->flag&LOCUS_SAMPLED)) continue;
				k2=loc->seg[X_PAT][i];
				if(k2<0) continue;
				theta=.5*(1.0-exp(-0.02*(loc->pos[X_PAT]-pos[X_PAT])));
				Ptp[k2]*=1.0-theta;
				Ptp[1-k2]*=theta;
				break;
			}
			z=Mtp[0]+Mtp[1];
			z1=Ptp[0]+Ptp[1];
			for(k=0;k<2;k++) {
				Mtp[k]/=z;
				Ptp[k]/=z1;
				id_array[i].tpp[X_MAT][k]=Mtp[k];
				id_array[i].tpp[X_PAT][k]=Ptp[k];
			}
			id_array[i].tp[X_MM_PM]=Mtp[X_MAT]*Ptp[X_MAT];
			id_array[i].tp[X_MM_PP]=Mtp[X_MAT]*Ptp[X_PAT];
			id_array[i].tp[X_MP_PM]=Mtp[X_PAT]*Ptp[X_MAT];
			id_array[i].tp[X_MP_PP]=Mtp[X_PAT]*Ptp[X_PAT];
		} else {	
			for(k=0;k<2;k++) id_array[i].tpp[X_MAT][k]=id_array[i].tpp[X_PAT][k]=0.5;
			for(k=0;k<4;k++) id_array[i].tp[k]=0.25;
		}
	}
}

static void convert_pos(int n,int nl,double *map1,double *map2,const struct loki *loki)
{
	int i,j;
	double x,x1,z,z1;
	
	for(i=0;i<n;i++) {
		x=pos_list[X_MAT][i];
		if(!loki->markers->sex_map) pos_list[X_PAT][i]=x;
		else {
			for(j=0;j<nl;j++) if(x>=locilist[j]->pos[X_MAT]) break;
			if(!j) {
				x1=locilist[0]->pos[X_MAT]-x;
				z=locilist[0]->pos[X_PAT]-map1[X_PAT];
				z1=locilist[0]->pos[X_MAT]-map1[X_MAT];
				x1=(z1>1.0e-12?x1*z/z1:0.0);
				pos_list[X_PAT][i]=locilist[j]->pos[X_PAT]-x1;
			} else if(j<nl) {
				z=(locilist[j]->pos[X_MAT]-locilist[j-1]->pos[X_MAT]);
				x1=(z>1.0e-12?(x-locilist[j-1]->pos[X_MAT])/z:0.0);
				pos_list[X_PAT][i]=locilist[j-1]->pos[X_PAT]+x1*(locilist[j]->pos[X_PAT]-locilist[j-1]->pos[X_PAT]);
			} else {
				x1=x-locilist[nl-1]->pos[X_MAT];
				z=map2[X_PAT]-locilist[nl-1]->pos[X_PAT];
				z1=map2[X_MAT]-locilist[nl-1]->pos[X_MAT];
				x1=(z1>1.0e-12?x1*z/z1:0.0);
				pos_list[X_PAT][i]=locilist[nl-1]->pos[X_PAT]+x1;
			}
		}
	}
}

static int get_pos_list(int i,int nl,const struct loki *loki)
{
	int n=0,j,k;
	double x;
	
	switch(loki->markers->linkage[i].ibd_est_type) {
	 case IBD_EST_DISCRETE:
		n=loki->markers->linkage[i].ibd_list->idx;
		for(j=0;j<n;j++) pos_list[X_MAT][j]=loki->markers->linkage[i].ibd_list->pos[j];
		convert_pos(n,nl,loki->markers->linkage[i].r1,loki->markers->linkage[i].r2,loki);
		break;
	 case IBD_EST_MARKERS:
		n=nl;
		for(j=0;j<nl;j++) {
			for(k=0;k<2;k++) pos_list[k][j]=locilist[j]->pos[k];
		}
		break;
	 case IBD_EST_GRID:
		/* Always do at least 1 evaluation */
		x=loki->markers->linkage[i].ibd_list->pos[0];
		pos_list[X_MAT][n++]=x;
		x+=loki->markers->linkage[i].ibd_list->pos[2];
		if(loki->markers->linkage[i].ibd_list->pos[2]>0.0) {
			for(;x<=loki->markers->linkage[i].ibd_list->pos[1];x+=loki->markers->linkage[i].ibd_list->pos[2]) pos_list[X_MAT][n++]=x;
		} else {
			for(;x>=loki->markers->linkage[i].ibd_list->pos[1];x+=loki->markers->linkage[i].ibd_list->pos[2]) pos_list[X_MAT][n++]=x;
		}
		convert_pos(n,nl,loki->markers->linkage[i].r1,loki->markers->linkage[i].r2,loki);
		break;
	}
	return n;
}

void Handle_IBD(const struct loki *loki)
{
	int i,i1,i2,i3,i4,j,k,k1,kk,ix=0,*genes[2],nl,s,comp,g0,g1,g2,g3,cs,ids,idd,np,kk1,ct;
	int *pp,n_comp,*comp_size,**ibd1[2];
	double x,z,ps[2],tp[2];
	struct Id_Record *id_array;
	
	id_array=loki->pedigree->id_array;
	n_comp=loki->pedigree->n_comp;
	comp_size=loki->pedigree->comp_size;
	genes[0]=loci+loki->markers->n_markers;
	genes[1]=genes[0]+loki->pedigree->ped_size;
	for(i=0;i<loki->markers->n_links;i++) {
		if(loki->markers->linkage[i].ibd_est_type)	{
			get_locuslist(locilist,i,&nl,1);
			gnu_qsort(locilist,(size_t)nl,(size_t)sizeof(void *),cmp_loci);
			np=get_pos_list(i,nl,loki);
			for(j=0;j<np;j++,ix++) {
				for(k=0;k<2;k++) ibd1[k]=ibd[k][ix];
				x=pos_list[0][j];
				for(k=0;k<nl;k++) if(locilist[k]->pos[0]>=x) break;
				for(i1=0;i1<2;i1++) ps[i1]=pos_list[i1][j];
				for(i1=0,comp=0;comp<n_comp;comp++) {
					Set_Trans(ps,locilist,k,nl,comp,loki);
					/* Fill in missing SI's */
					for(i2=0;i2<comp_size[comp];i2++) if(id_array[i1+i2].sire) {
						for(k1=0;k1<2;k1++) {
							tp[0]=id_array[i1+i2].tpp[k1][0];
							tp[1]=id_array[i1+i2].tpp[k1][1];
							z=tp[0]+tp[1];
							if(tp[0] && ranf()*z<=tp[0]) seg[k1][i1+i2]=0;
							else seg[k1][i1+i2]=1;
						}
					}
					i1+=comp_size[comp];
				}
				for(i1=0,comp=0;comp<n_comp;comp++) if(!(loki->pedigree->singleton_flag[comp])) {
					pp=inb_sparse[comp];
					cs=comp_size[comp];
					kk=pp[0];
					for(k1=i2=i3=0;i2<cs;i2++,i1++) {
						ids=id_array[i1].sire;
						idd=id_array[i1].dam;
						if(ids--) {
							s=seg[X_PAT][i1];
							genes[X_PAT][i1]=genes[s][ids];
						} else genes[X_PAT][i1]= ++k1;
						if(idd--) {
							s=seg[X_MAT][i1];
							genes[X_MAT][i1]=genes[s][idd];
						} else genes[X_MAT][i1]= ++k1;
						g0=genes[0][i1];
						kk1=pp[i2+1];
						g1=genes[1][i1];
						for(;kk<kk1;kk++) {
							i4=pp[kk];
							g2=genes[0][i4];
							g3=genes[1][i4];
							if(g0==g2) {
								if(g1==g3) {
									ibd1[1][comp][i3]+=2;
									ct=(g0==g1)?4:2;
								} else {
									ct=1;
									if(g0==g3) ct++;
									if(g1==g2) ct++;
								}
							} else if(g0==g3) {
								if(g1==g2) {
									ibd1[1][comp][i3]+=2;
									ct=2;
								} else ct=(g1==g3)?2:1;
							} else {
								ct=0;
								if(g1==g2) ct++;
								if(g1==g3) ct++;
							}
							ibd1[0][comp][i3++]+=ct;
						}
						if(inbr[i1]) ibd1[0][comp][i3++]+=(g0==g1)?4:2;
					}
				}
			}
		}
	}
	return;
}

double score_ibd(int locus,int *k5,int comp,int affn,int *aff,double *pairs,const struct loki *loki)
{
	int i,i1,i2,j,k,k2,k3,k4,**genes,nl,**ss;
	double l=0.0,z,tp[2];
	struct Locus *loc;
	struct Id_Record *id_array;
	
	if(loki->markers->marker[locus].n_all1[comp]<2) return l;
	*k5=k4=0;
	id_array=loki->pedigree->id_array;
	loc=&loki->markers->marker[locus].locus;
	genes=loc->genes;
	ss=loc->seg;
	if(!loki->params.si_mode) {
		i=loc->link_group;
		get_locuslist(locilist,i,&nl,1);
		gnu_qsort(locilist,(size_t)nl,(size_t)sizeof(void *),cmp_loci);
		for(k=0;k<nl;k++) if(locilist[k]==loc) break;
		i1=loki->pedigree->comp_start[comp];
		Set_Trans(loc->pos,locilist,k,nl,comp,loki);
		for(i2=0;i2<loki->pedigree->comp_size[comp];i2++) {
			for(k2=0;k2<2;k2++) {
				if(ss[k2][i1+i2]!=-1) {
					tp[0]=id_array[i1+i2].tpp[k2][0];
					tp[1]=id_array[i1+i2].tpp[k2][1];
					z=tp[0]+tp[1];
					if(tp[0] && ranf()*z<=tp[0]) seg[k2][i1+i2]=0;
					else seg[k2][i1+i2]=1;
				} else seg[k2][i1+i2]=ss[k2][i1+i2];
			} 
		}
		(void)pass_founder_genes2(locus,comp,seg,loki);
	} else (void)pass_founder_genes2(locus,comp,ss,loki);
	for(i=0;i<affn;i++) {
		i1=aff[i];
		for(j=0;j<=i;j++) {
			i2=aff[j];
			z=0.0;
			for(k2=0;k2<2;k2++) for(k3=0;k3<2;k3++) if(genes[k2][i1]==genes[k3][i2]) z+=.25;
			if(pairs[k4]>0.0) {
				l+=z/pairs[k4];
				(*k5)++;
			} else assert(z==0.0);
			k4++;
		}
	}
	return l; 
}

static void print_pair(FILE *fptr,int i,int j,const struct loki *loki)
{
	if(loki->pedigree->family_id) {
		print_orig_family(fptr,i,0);
		(void)fputc(' ',fptr);
	}
	print_orig_id1(fptr,i);
	(void)fputc(' ',fptr);
	print_orig_id1(fptr,j);
}

static FILE *open_ibd_file(char *name,const struct loki *loki)
{
	int i,i1,j,k=0,md,cpress;
	char *fname;
	FILE *fptr;
	
	md=loki->params.ibd_mode;
	cpress=loki->compress->default_compress;
	if((md&COMPRESS_IBD) && cpress!=COMPRESS_NONE) {
		j=0;
		i=(int)strlen(name);
		i1=(int)strlen(suff[cpress]);
		if(i>i1 && !strcmp(name+i-i1,suff[cpress])) j=1;
		if(!j) {
			fname=lk_malloc(i+1+i1);
			memcpy(fname,name,i);
			strncpy(fname+i,suff[cpress],i1+1);
			k=1;
		} else fname=name;
		errno=0;
		i=child_open1(WRITE,fname,loki->compress->comp_path[cpress][0],cpress==COMPRESS_ZIP?"-":0);
		fptr=fdopen(i,"w");
		if(errno && errno!=ESPIPE) i=1;
		else i=0;
		errno=0;
	} else {
		fname=name;
		fptr=fopen(fname,"w");
		i=0;
	}
	if(i || !fptr) abt(__FILE__,__LINE__,"%s(): File Error.  Couldn't open '%s' for output\n",__func__,fname);
	printf("Writing IBD matrices to file %s\n",fname);
	if(k) free(fname);
	return fptr;
}

int read_ibd_dump(FILE *fdump,int *n_ibd,char *tmp,const struct loki *loki)
{
	int link,k,j,k1,k2=0,comp,i=0;
	char *tmp1;
	string *s=0;
	void *tbuf=0;
	
	for(k=link=0;link<loki->markers->n_links;link++) if(loki->markers->linkage[link].ibd_est_type) k++;
	if(k) {
		(void)fputs("[IBD matrices] ",stdout);
		(void)fflush(stdout);
		/* Count no. locations */
		for(link=j=k1=0;link<loki->markers->n_links;link++) if(loki->markers->linkage[link].ibd_est_type) {
			k=0;
			switch(loki->markers->linkage[link].ibd_est_type) {
			 case IBD_EST_DISCRETE:
				k=loki->markers->linkage[link].ibd_list->idx;
				break;
			 case IBD_EST_MARKERS:
				k=loki->markers->linkage[link].n_markers;
				break;
			 case IBD_EST_GRID:
				k=1+(int)(.5+(loki->markers->linkage[link].ibd_list->pos[1]-loki->markers->linkage[link].ibd_list->pos[0])/loki->markers->linkage[link].ibd_list->pos[2]);
				break;
			}
			if(k>k1) k1=k;
			j+=k;
		}
		if(*tmp++!=',') i=__LINE__;
		if(!i) {
			k1=(int)strtol(tmp,&tmp1,16);
			if(k1!=j) i=__LINE__;
		}
		if(!i && *tmp1++!=',') i=__LINE__;
		if(!i) {
			*n_ibd=(int)strtol(tmp1,&tmp,16);
			if(*tmp!='\n') i=__LINE__;
		}
		for(comp=0;!i && comp<loki->pedigree->n_comp;comp++) if(n_pairs[comp]) {
			s=fget_string(fdump,s,&tbuf);
			tmp=get_cstring(s);
			if(!s->len) i=__LINE__;
			else if(strncmp(tmp,"LKCM:",5)) i=__LINE__;
			tmp+=5;
			if(!i) {
				k=(int)strtol(tmp,&tmp1,16);
				if(k!=comp) i=__LINE__;
			}
			if(!i && *tmp1++!=',') i=__LINE__;
			if(!i) {
				k=(int)strtol(tmp1,&tmp,16);
				if(k!=n_pairs[comp]) i=__LINE__;
			}
			if(!i && *tmp!='\n') i=__LINE__;
			for(k=0;!i && k<n_pairs[comp];k++) {
				for(k1=0;!i && k1<j;k1++) {
					s=fget_string(fdump,s,&tbuf);
					tmp=get_cstring(s);
					if(!s->len) i=__LINE__;
					else k2=(int)strtol(tmp,&tmp1,16);
					if(!i) {
						ibd[0][k1][comp][k]=k2;
						if(*tmp1++!=',') i=__LINE__;
					}
					if(!i) {
						k2=(int)strtol(tmp1,&tmp,16);
						ibd[1][k1][comp][k]=k2;
						if(*tmp!='\n') i=__LINE__;
					}
				}
			}
		}
	}
	if(s) free_string(s);
	if(tbuf) free_fget_buffer(&tbuf);
	return -i;
}

int write_ibd_dump(FILE *fdump,int n_ibd,const struct loki *loki)
{
	int link,k,j,k1,k2,comp,i=0;
	
	for(k=link=0;link<loki->markers->n_links;link++) if(loki->markers->linkage[link].ibd_est_type) k++;
	if(k) {
		(void)fputs("[IBD matrices] ",stdout);
		(void)fflush(stdout);
		/* Count no. locations */
		for(link=j=k1=0;link<loki->markers->n_links;link++) if(loki->markers->linkage[link].ibd_est_type) {
			k=0;
			switch(loki->markers->linkage[link].ibd_est_type) {
			 case IBD_EST_DISCRETE:
				k=loki->markers->linkage[link].ibd_list->idx;
				break;
			 case IBD_EST_MARKERS:
				k=loki->markers->linkage[link].n_markers;
				break;
			 case IBD_EST_GRID:
				k=1+(int)(.5+(loki->markers->linkage[link].ibd_list->pos[1]-loki->markers->linkage[link].ibd_list->pos[0])/loki->markers->linkage[link].ibd_list->pos[2]);
				break;
			}
			if(k>k1) k1=k;
			j+=k;
		}
		if(fprintf(fdump,",%x,%x\n",j,n_ibd)<0) i=1;
		for(comp=0;!i && comp<loki->pedigree->n_comp;comp++) if(n_pairs[comp]) {
			if(fprintf(fdump,"LKCM:%x,%x\n",comp,n_pairs[comp])<0) i=1;
			for(k=0;!i && k<n_pairs[comp];k++) {
				for(k1=0;!i && k1<j;k1++) {
					k2=ibd[0][k1][comp][k];
					if(k2) {
						if(fprintf(fdump,"%x,",k2)<0) i=1;
					} else {
						if(fputc(',',fdump)<0) i=1;
					}
					k2=ibd[1][k1][comp][k];
					if(k2) {
						if(fprintf(fdump,"%x\n",k2)<0) i=1;
					} else {
						if(fputc('\n',fdump)<0) i=1;
					}
				}
			}
		}
	}
	return i;
}

void Output_Sample_IBD(int lp,int n_ibd,const struct loki *loki)
{
	int i,i1,i2,j,k,k1,l,link,comp,n_long,ids,idd,np,nl=0,*ibd1[2];
	double z;
	unsigned long *tp,*tp1,*tp2;
	FILE *fptr;
	
	if(lp<1) return;
	fptr=open_ibd_file(loki->names[LK_IBDFILE],loki);
	(void)fprintf(fptr,"Iteration %d\n",lp);
	z=0.5/(double)n_ibd;
	for(k=link=0;link<loki->markers->n_links;link++) {
		if(loki->markers->linkage[link].ibd_est_type) {
			(void)fprintf(fptr,"\n**Linkage group %s:\n",loki->markers->linkage[link].name);
			get_locuslist(locilist,link,&nl,1);
			gnu_qsort(locilist,(size_t)nl,(size_t)sizeof(void *),cmp_loci);
			np=get_pos_list(link,nl,loki);
			for(l=0;l<np;l++) {
				if(loki->markers->sex_map) (void)fprintf(fptr,"\n**Position = %g,%g\n",pos_list[X_MAT][l],pos_list[X_PAT][l]);
				else (void)fprintf(fptr,"\n**Position = %g\n",pos_list[X_MAT][l]);
				tp=founders;
				for(i1=comp=0;comp<loki->pedigree->n_comp;comp++) if(!(loki->pedigree->singleton_flag[comp])) {
					n_long=n_longs[comp];
					ibd1[0]=ibd[0][k+l][comp];
					ibd1[1]=ibd[1][k+l][comp];
					tp2=tp;
					for(i2=0,i=0;i<loki->pedigree->comp_size[comp];i++,tp+=n_long) {
						ids=loki->pedigree->id_array[i1+i].sire-1-i1;
						idd=loki->pedigree->id_array[i1+i].dam-1-i1;
						tp1=tp2;
						for(j=0;j<i;j++,tp1+=n_long) {
							for(k1=0;k1<n_long;k1++) if(tp[k1]&tp1[k1]) break;
							if(k1==n_long) continue;
							if(!(inbr[i1+i]||inbr[i1+j])&&(ids==j||idd==j)) {
								print_pair(fptr,i1+i+1,i1+j+1,loki);
								(void)fputs(" 0.5 0\n",fptr);
							} else {
								if(ibd1[0][i2]) {
									print_pair(fptr,i1+i+1,i1+j+1,loki);
									(void)fprintf(fptr," %g %g\n",(double)ibd1[0][i2]*z,(double)ibd1[1][i2]*z);
								}
								i2++;
							}
						}
						print_pair(fptr,i1+i+1,i1+i+1,loki);
						if(inbr[i1+i]) (void)fprintf(fptr," %g 1\n",(double)ibd1[0][i2++]*z);
						else (void)fputs(" 1 1\n",fptr);
					}
					i1+=i;
				}
			}
			k+=l;
		}
	}
	(void)fputc('\n',fptr);
	fclose(fptr);
}

/* Note that these are incorrect in the case of inbreeding - this is a problem with the file format
 * so there's not much that I can do! 
 */
void Output_Merlin_IBD(int lp,const struct loki *loki)
{
	int i,i1,i2,j,k,k1,l,link,comp,n_long,ids,idd,np=0,nl,*ibd1[2];
	double x,z,s0,s1,s2;
	unsigned long *tp,*tp1,*tp2;
	FILE *fptr;
	
	if(lp<1) return;
	fptr=open_ibd_file(loki->names[LK_IBDFILE],loki);
	z=0.5/(double)lp;
	tp=founders;
	for(i1=comp=0;comp<loki->pedigree->n_comp;comp++) if(!(loki->pedigree->singleton_flag[comp])) {
		n_long=n_longs[comp];
		for(link=k=0;link<loki->markers->n_links;link++) {
			if(loki->markers->linkage[link].ibd_est_type) {
				get_locuslist(locilist,link,&nl,1);
				gnu_qsort(locilist,(size_t)nl,(size_t)sizeof(void *),cmp_loci);
				np=get_pos_list(link,nl,loki);
				for(l=0;l<np;l++) {
					x=pos_list[X_MAT][l];
					tp=founders;
					ibd1[0]=ibd[0][k][comp];
					ibd1[1]=ibd[1][k][comp];
					tp2=tp;
					for(i2=0,i=0;i<loki->pedigree->comp_size[comp];i++,tp+=n_long) {
						ids=loki->pedigree->id_array[i1+i].sire-1-i1;
						idd=loki->pedigree->id_array[i1+i].dam-1-i1;
						tp1=tp2;
						for(j=0;j<i;j++,tp1+=n_long) {
							for(k1=0;k1<n_long;k1++) if(tp[k1]&tp1[k1]) break;
							if(k1<n_long) {
								if(!(inbr[i1+i]||inbr[i1+j])&&(ids==j||idd==j)) {
									print_pair(fptr,i1+i+1,i1+j+1,loki);
									(void)fprintf(fptr," %.3f 0.0 1.0 0.0\n",x);
								} else {
									print_pair(fptr,i1+i+1,i1+j+1,loki);
									s2=(double)ibd1[1][i2]*z;
									s1=2.0*z*(double)(ibd1[0][i2]-ibd1[1][i2]);
									s0=1.0-s1-s2;
									if(s0<0) s0=0.0;
									(void)fprintf(fptr," %.3f %.5f %.5f %.5f\n",x,s0,s1,s2);
									i2++;
								}
							} else {
								print_pair(fptr,i1+i+1,i1+j+1,loki);
								(void)fprintf(fptr," %.3f 1.0 0.0 0.0\n",x);
							}
						}
						print_pair(fptr,i1+i+1,i1+i+1,loki);
						if(inbr[i1+i]) i2++;
						(void)fprintf(fptr," %.3f 0.0 0.0 1.0\n",x);
					}
					k++;
				}
			}
		}
		i1+=loki->pedigree->comp_size[comp];
	}
	/* Do singletons */
	for(comp=0;comp<loki->pedigree->n_comp;comp++) if(loki->pedigree->singleton_flag[comp]) {
		for(link=k=0;link<loki->markers->n_links;link++) {
			if(loki->markers->linkage[link].ibd_est_type) {
				get_locuslist(locilist,link,&nl,1);
				gnu_qsort(locilist,(size_t)nl,(size_t)sizeof(void *),cmp_loci);
				np=get_pos_list(link,nl,loki);
				for(l=0;l<np;l++) {
					x=pos_list[X_MAT][l];
					for(i=0;i<loki->pedigree->comp_size[comp];i++) {
						ids=loki->pedigree->id_array[i1+i].sire-1-i1;
						idd=loki->pedigree->id_array[i1+i].dam-1-i1;
						print_pair(fptr,i1+i+1,i1+i+1,loki);
						(void)fprintf(fptr," %.3f 0.0 0.0 1.0d\n",x);
					}
				}
			}
		}
	}
	fclose(fptr);
}

void Output_Solar_IBD(int lp,int *trans,const struct loki *loki)
{
	int i,i1,i2,j,k,kk,l,link,comp,ids,idd,np=0,nl,dlen,clen,*pp,s1,s2,ibd_md,cpress,slen=0,*ibd1[2],ct;
	size_t sz;
	double x,z;
	char *dname,*fname;
	struct stat sb;
	FILE *fptr;
	
	if(lp<1) return;
	ibd_md=loki->params.ibd_mode;
	if(ibd_md&COMPRESS_IBD) {
		cpress=loki->compress->default_compress;
		if(cpress!=COMPRESS_NONE) slen=strlen(suff[cpress]);
	} else cpress=COMPRESS_NONE;
	z=0.5/(double)lp;
	if(loki->names[LK_IBDDIR]) dname=loki->names[LK_IBDDIR];
	else dname=make_file_name("_ibd");
	/* Check if output dir exists */
	dlen=(int)strlen(dname);
	if(!stat(dname,&sb)) {
		/* Check if it is a directory */
		if(!(sb.st_mode&S_IFDIR)) {
			fprintf(stderr,"IBD output destination '%s' exists, but is not a directory\n",dname);
			return;
		}
		/* Check if we have write and execute permission */
		if((sb.st_mode&(S_IWUSR|S_IXUSR))!=(S_IWUSR|S_IXUSR)) {
			fprintf(stderr,"Do not have permission to create files in '%s'\n",dname);
			return;
		}
	} else {
		if(errno==ENOENT) { /* Directory does not exist */
			/* Try to create directory */
			if(mkdir(dname,0755)) {
				fprintf(stderr,"Problem creating IBD output directory '%s':",dname);
				perror(0);
				return;
			}
		} else {
			fprintf(stderr,"File problem with IBD output directory '%s':",dname);
			perror(0);
			return;
		}
	}
	for(link=k=0;link<loki->markers->n_links;link++) {
		if(loki->markers->linkage[link].ibd_est_type) {
			clen=(int)strlen(loki->markers->linkage[link].name);
			sz=dlen+8+clen+16;
			if(ibd_md&COMPRESS_IBD) sz+=slen;
			fname=lk_malloc(sz);
			get_locuslist(locilist,link,&nl,1);
			gnu_qsort(locilist,(size_t)nl,(size_t)sizeof(void *),cmp_loci);
			np=get_pos_list(link,nl,loki);
			for(l=0;l<np;l++) {
				x=pos_list[X_MAT][l];
				if(loki->markers->linkage[link].ibd_est_type==IBD_EST_MARKERS) 
				  snprintf(fname,sz,"%s/mibd.%s",dname,loki->markers->marker[locilist[l]->index].name);
				else snprintf(fname,sz,"%s/mibd.%s.%-15g",dname,loki->markers->linkage[link].name,x);
				qstrip(fname);
				i=0;
				if(cpress!=COMPRESS_NONE) {
					i=strlen(fname);
					strncpy(fname+i,suff[cpress],sz-i);
					errno=0;
					i=child_open1(WRITE,fname,loki->compress->comp_path[cpress][0],cpress==COMPRESS_ZIP?"-":0);
					fptr=fdopen(i,"w");
					if(errno && errno!=ESPIPE) i=1;
					else i=0;
					errno=0;
				} else fptr=fopen(fname,"w");
				printf("Writing IBD matrices to file %s\n",fname);
				if(!fptr || i) {
					(void)fprintf(stderr,"[%s:%d] %s(): File Error.  Couldn't open '%s' for output\n",__FILE__,__LINE__,__func__,fname);
					continue;
				}
				for(i1=comp=0;comp<loki->pedigree->n_comp;comp++) if(!(loki->pedigree->singleton_flag[comp])) {
					pp=inb_sparse1[comp];
					ibd1[0]=ibd[0][k][comp];
					ibd1[1]=ibd[1][k][comp];
					kk=pp[0];
					for(i2=0,i=0;i<loki->pedigree->comp_size[comp];i++) {
						ids=loki->pedigree->id_array[i1+i].sire-1;
						idd=loki->pedigree->id_array[i1+i].dam-1;
						s1=trans[i1+i];
						for(;kk<pp[i+1];kk++) {
							j=pp[kk];
							s2=trans[j];
							if(!(inbr[i1+i]||inbr[j])&&(ids==j||idd==j)) {
								if(s1>s2) (void)fprintf(fptr,"%5d %5d",s1,s2);
								else (void)fprintf(fptr,"%5d %5d",s2,s1);
								(void)fprintf(fptr," %10.7f %10.7f\n",0.5,0.0);
							} else {
								ct=ibd1[0][i2];
								if(ct) {
									if(s1>s2) (void)fprintf(fptr,"%5d %5d",s1,s2);
									else (void)fprintf(fptr,"%5d %5d",s2,s1);
									(void)fprintf(fptr," %10.7f %10.7f\n",(double)ct*z,(double)ibd1[1][i2]*z);
								}
								i2++;
							}
						}
						(void)fprintf(fptr,"%5d %5d",trans[i1+i],trans[i1+i]);
						if(inbr[i1+i]) {
							ct=ibd1[0][i2++];
							if(ct) (void)fprintf(fptr," %10.7f %10.7f\n",(double)ct*z,1.0);
						} else (void)fprintf(fptr," %10.7f %10.7f\n",1.0,1.0);
					}
					i1+=loki->pedigree->comp_size[comp];
				}
				fclose(fptr);
				k++;
			}
			free(fname); 
		}
	}
	if(!loki->names[LK_IBDDIR]) free(dname);
	while(waitpid(-1,&i,WNOHANG)>0);
}
