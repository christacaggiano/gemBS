/****************************************************************************
 *                                                                          *
 *     Loki - Programs for genetic analysis of complex traits using MCMC    *
 *                                                                          *
 *             Simon Heath - University of Washington                       *
 *                                                                          *
 *                        July 1997                                         *
 *                                                                          *
 * gen_elim.c:                                                              *
 *                                                                          *
 * (1) Use pattern of marker data to prune the pedigree and split it into   *
 * components on a marker by marker basis.                                  *
 *                                                                          *
 * (2) Recode unused alleles in each component                              *
 *                                                                          *
 * (3) Perform genotype elimination.                                        *
 *                                                                          *
 * (4) Perform set recoding a la O'Connell & Weeks                          *
 *                                                                          *
 * (5) Determine a peeling sequence                                         *
 *                                                                          *
 * (6) Perform 'logical peeling' to check genotype consistency and          *
 * allow optimizations for later peeling runs                               *
 *                                                                          *
 * Copyright (C) Simon C. Heath 1997, 2000, 2002                            *
 * This is free software.  You can distribute it and/or modify it           *
 * under the terms of the Modified BSD license, see the file COPYING        *
 *                                                                          *
 ****************************************************************************/

#include <config.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef USE_DMALLOC
#include <dmalloc.h>
#endif
#include <math.h>
#include <stdio.h>
#include <errno.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include "utils.h"
#include "libhdr.h"
#include "scan.h"
#include "y.tab.h"
#include "prep_peel.h"
#include "min_deg.h"
#include "prep_utils.h"
#include "snprintf.h"

extern int catch_sigs,sig_caught;
static int *allele_trans,bad_cnt,rec_gen_flag,ge_option;
static int *perm,id,*famflag,r_func_size,n_rfuncs,*involved,*prev_inv,*true_involved,*rf_flag;
static int no_peel_flag,n_prev_inv,silent_flag,n_all_old;
static lk_ulong *temp_set,*id_set[2],mask,**all_set,*req_set[3];
static struct R_Func *r_func;
static char *marker_name;
static struct Peelseq_Head peelseq_head;
int trace_peel;

static void FlagFam(const int fam)
{
	int j,k,l,ids,idd;
	
	ids=family[fam].sire;
	idd=family[fam].dam;
	if(!ids) ABT_FUNC("Internal error - fix me\n");
	j=id_array[ids-1].family;
	if(j) famflag[j-1]|=1;
	for(k=0;k<id_array[ids-1].nfam;k++)	{
		j=id_array[ids-1].famlist[k];
		famflag[j]|=1;
	}
	j=id_array[idd-1].family;
	if(j) famflag[j-1]|=1;
	for(k=0;k<id_array[idd-1].nfam;k++)	{
		j=id_array[idd-1].famlist[k];
		famflag[j]|=1;
	}
	for(k=0;k<family[fam].nkids;k++)	{
		j=family[fam].kids[k];
		for(l=0;l<id_array[j].nfam;l++) {
			famflag[id_array[j].famlist[l]]|=1;
		}
	}
}

void print_orig_alleles(FILE *fptr,const int ind, const int locus,const int linktype)
{
	int i,ch,fg=0;
	
	if(linktype==LINK_Y || (linktype==LINK_X && id_array[ind-1].sex==1)) fg=1;
	if(id_array[ind-1].haplo[0]) {
		for(i=0;i<2;i++) {
			ch=id_array[ind-1].haplo[i][locus];
			if(ch) {
				ch=allele_trans[ch-1];
				if(factor_recode[n_factors+locus][ch]->type==STRING)
				  (void)fputs(factor_recode[n_factors+locus][ch]->data.string,fptr);
				else (void)fprintf(fptr,"%ld",factor_recode[n_factors+locus][ch]->data.value);
			} else (void)fputc('*',fptr);
			if(fg) break;
			if(!i) (void)fputc(',',fptr);
		}
	} else {
		if(fg) (void)fputc('*',fptr);
		else (void)fputs("*,*",fptr);
	}
}

static void print_rec_all(FILE *fptr, int ch,const int locus)
{
	ch=allele_trans[ch];
	if(ch== -1) (void)fputs("__LUMP__",fptr);
	else {
		if(factor_recode[n_factors+locus][ch]->type==STRING)
		  (void)fputs(factor_recode[n_factors+locus][ch]->data.string,fptr);
		else (void)fprintf(fptr,"%ld",factor_recode[n_factors+locus][ch]->data.value);
	}
}

static void print_code_rec_all(FILE *fptr,int ch,lk_ulong a,int locus)
{
	int l,k;
	
	if(a&(1<<ch)) {
		l=k=0;
		while(a) {
			if(a&1) {
				(void)fputc(k++?',':'[',fptr);
				print_rec_all(fptr,l,locus);
			}
			l++;
			a>>=1;
		};
		(void)fputc(']',fptr);
	} else print_rec_all(fptr,ch,locus);
}

static void print_alls(FILE *fptr,const int i,const int locus,const int n_all,const int linktype,const int flag)
{
	int j,k,k1,l,fg=0;
	lk_ulong a,*b,m;
	
	if(!(b=calloc(n_all,sizeof(lk_ulong)))) ABT_FUNC(MMsg);
	j=ped_recode1[i-1]-1-id;
	if(linktype==LINK_Y || (linktype==LINK_X && id_array[i-1].sex==1)) fg=1;
	m=(1<<n_all)-1;
	for(k1=k=0;k<n_all;k++)	{
		a=all_set[k][j];
		if(a==m) k1++;
		for(l=0;l<n_all;l++) if(a&(1<<l)) b[l]|=1<<k;
	}
	if(k1==n_all) {
		if(fg) (void)fputs("(*)",fptr);
		else (void)fputs("(*,*)",fptr);
	} else if(linktype==LINK_Y) {
		a=all_set[0][j];
		l=0;
		if(a==m) (void)fputs("(*)",fptr);
		else while(a) {
			if(a&1) {
				(void)fputc('(',fptr);
				print_rec_all(fptr,l,locus);
				(void)fputs(") ",fptr);
			}
			l++;
			a>>=1;
		}
	} else if(linktype==LINK_X && id_array[i-1].sex==1) {
		for(k=0;k<n_all;k++) if(!all_set[k][j]) break;
		if(k==n_all) (void)fputs("(*)",fptr);
		else for(k=0;k<n_all;k++) if(all_set[k][j]) {
			(void)fputc('(',fptr);
			print_rec_all(fptr,k,locus);
			(void)fputs(") ",fptr);
		}
	} else {
		for(k=0;k<n_all;k++) {
			l=0;
			a=all_set[k][j];
			if(a==m) {
				(void)fputs("(*,",fptr);
				if(flag) print_code_rec_all(fptr,k,req_set[X_MAT][j],locus);
				else print_rec_all(fptr,k,locus);
				(void)fputs(") ",fptr);
			} else while(a) {
				if(b[l]!=m && (a&1)) {
					(void)fputc('(',fptr);
					if(flag) print_code_rec_all(fptr,l,req_set[X_PAT][j],locus);
					else print_rec_all(fptr,l,locus);
					(void)fputc(',',fptr);
					if(flag) print_code_rec_all(fptr,k,req_set[X_MAT][j],locus);
					else print_rec_all(fptr,k,locus);
					(void)fputs(") ",fptr);
				}
				l++;
				a>>=1;
			}
		}
		for(l=0;l<n_all;l++) if(b[l]==m) {
			(void)fputc('(',fptr);
			if(flag) print_code_rec_all(fptr,l,req_set[X_PAT][j],locus);
			else print_rec_all(fptr,l,locus);
			(void)fputs(",*) ",fptr);
		}
	}
	(void)fputc('\n',fptr);
	free(b);
}

static void PrintFamily(const int fam,const int locus,const int n_all,const int linktype,const int flag)
{
	int ids,idd,kid,i;
	
	ids=family[fam].sire;
	idd=family[fam].dam;
	if(family_id) {
		(void)fputs("Family ",stderr);
		print_orig_family(stderr,family[fam].kids[0]+1,0);
		(void)fputs(" \n",stderr);
	}
	if(ids) {
		(void)fputs("Father: ",stderr);
		print_orig_id1(stderr,ids,0);
		(void)fputs(" [",stderr);
		print_orig_alleles(stderr,ids,locus,linktype);
		(void)fputs("]\n        ",stderr);
		print_alls(stderr,ids,locus,n_all,linktype,flag);
	}
	if(idd && linktype!=LINK_Y) {
		(void)fputs("Mother: ",stderr);
		print_orig_id1(stderr,idd,0);
		(void)fputs(" [",stderr);
		print_orig_alleles(stderr,idd,locus,linktype);
		(void)fputs("]\n        ",stderr);
		print_alls(stderr,idd,locus,n_all,linktype,flag);
	}
	for(i=0;i<family[fam].nkids;i++) {
		kid=family[fam].kids[i];
		if(id_array[kid].flag&IS_PRUNED) continue;
		if(linktype==LINK_Y && id_array[kid].sex==2) continue;
		(void)fputs("     -> ",stderr);
		print_orig_id1(stderr,++kid,0);
		(void)fputs(" [",stderr);
		print_orig_alleles(stderr,kid,locus,linktype);
		(void)fputs("]\n  ",stderr);
		print_alls(stderr,kid,locus,n_all,linktype,flag);
	}
}

static int DoFamily(const int fam,const int n_all,const int locus,const int linktype)
{
	int i,j,k,l,i1,j_1,k1,l1,m,ids,idd,nc=0,fg,kid,change=0;
	int nkids,*tmp,nmc;
	lk_ulong a,b,c,cm,cm1,*t_all1,*t_all2;
	static int *kids,*m_set1,*m_set2,max_kids,max_k,max_mc;
	static lk_ulong *ccm,*ccm1,*tt_all;

	if(fam<0) {
		if(kids) free(kids);
		if(tt_all) free(tt_all);
		if(m_set1) free(m_set1);
		return 0;
	}
	ids=family[fam].sire;
	if(!ids)	{
		famflag[fam]&=~1;
		return 0;
	}
	tmp=family[fam].kids;
	j=family[fam].nkids;
	ids=ped_recode1[ids-1]-1-id;
	idd=ped_recode1[family[fam].dam-1]-1-id;
	nkids=0;
	k=j*n_all+2*j;
	if(j>max_kids) {
		max_kids=j;
		if(kids) {
			if(!(kids=realloc(kids,sizeof(int)*j))) ABT_FUNC(MMsg);
		} else {
			if(!(kids=malloc(sizeof(int)*j))) ABT_FUNC(MMsg);
		}
	} 
	if(k>max_k) {
		max_k=k;
		if(tt_all) {
			if(!(tt_all=realloc(tt_all,sizeof(lk_ulong)*k))) ABT_FUNC(MMsg);
		} else {
			if(!(tt_all=malloc(sizeof(lk_ulong)*k))) ABT_FUNC(MMsg);
		}
	}
	k=n_all*n_all;
	if(k>max_mc) {
		max_mc=k;
		if(m_set1) {
			if(!(m_set1=realloc(m_set1,sizeof(int)*k*2))) ABT_FUNC(MMsg);
		} else {
			if(!(m_set1=malloc(sizeof(int)*k*2))) ABT_FUNC(MMsg);
		}
	}
	m_set2=m_set1+k;
	ccm=tt_all+j*n_all;
	ccm1=ccm+j;
	t_all1=tt_all;
	for(i=0;i<j;i++) {
		kid=tmp[i];
		if(id_array[kid].flag&IS_PRUNED) continue;
		kid=ped_recode1[kid]-1-id;
		for(k1=0;k1<n_all;k1++) t_all1[k1]=all_set[k1][kid];
		if(linktype==LINK_AUTO) {
			t_all2=tt_all;
			for(k=0;k<nkids;k++) {
				for(k1=0;k1<n_all;k1++) if(t_all1[k1]!=t_all2[k1]) break;
				if(k1==n_all) break;
				t_all2+=n_all;
			}
		} else k=nkids;
		if(k==nkids) {
			a=c=0;
			for(k=0;k<n_all;k++) {
				if((b=t_all1[k])) {
					a|=b;
					c|=1<<k;
				}
			}
			ccm[nkids]=a;
			ccm1[nkids]=c;
			kids[nkids++]=kid;
			t_all1+=n_all;
		}
	}
	for(i=0;i<n_all;i++) temp_set[i]=id_set[0][i]=id_set[1][i]=0;
	if(linktype==LINK_AUTO)	{
		for(k1=1,nmc=k=0;k<n_all;k++,k1<<=1) {
			b=all_set[k][idd];
			l=0;
			l1=1;
			while(b) {
				if((b&1)&&(l<=k || !(all_set[l][idd]&k1))) {
					cm=k1|l1;
					for(m=0;m<nkids;m++) if(!(cm&ccm1[m])) break;
					if(m==nkids) {
						m_set1[nmc]=k;
						m_set2[nmc++]=l;
					}
				}
				b>>=1;
				l++;
				l1<<=1;
			}
		}
		switch(nkids) {
		 case 1:
			for(i1=1,i=0;i<n_all;i++,i1<<=1)	{
				a=all_set[i][ids];
				j=0;
				j_1=1;
				while(a) {
					if((a&1)&&(j<=i || !(all_set[j][ids]&i1))) {
						cm=i1|j_1;
						for(m=0;m<nkids;m++) if(!(cm&ccm[m])) break;
						if(m==nkids) for(k1=0;k1<nmc;k1++) {
							k=m_set1[k1];
							l=m_set2[k1];
							if((tt_all[k]&cm)||(tt_all[l]&cm)) {
								temp_set[k]|=cm;
								temp_set[l]|=cm;
								id_set[0][i]|=j_1;
								id_set[0][j]|=i1;
								id_set[1][k]|=1<<l;
								id_set[1][l]|=1<<k;
								nc++;
							}
						}
					}
					a>>=1;
					j++;
					j_1<<=1;
				}
			}
			break;
		 case 2:
			t_all1=tt_all+n_all;
			for(i1=1,i=0;i<n_all;i++,i1<<=1)	{
				a=all_set[i][ids];
				j=0;
				j_1=1;
				while(a) {
					if((a&1)&&(j<=i || !(all_set[j][ids]&i1))) {
						cm=i1|j_1;
						for(m=0;m<nkids;m++) if(!(cm&ccm[m])) break;
						if(m==nkids) for(k1=0;k1<nmc;k1++) {
							k=m_set1[k1];
							l=m_set2[k1];
							if(((tt_all[k]&cm)||(tt_all[l]&cm))&&((t_all1[k]&cm)||(t_all1[l]&cm))) {
								temp_set[k]|=cm;
								temp_set[l]|=cm;
								id_set[0][i]|=j_1;
								id_set[0][j]|=i1;
								id_set[1][k]|=1<<l;
								id_set[1][l]|=1<<k;
								nc++;
							}
						}
					}
					a>>=1;
					j++;
					j_1<<=1;
				}
			}
			break;
		 case 3:
			t_all1=tt_all+n_all;
			t_all2=t_all1+n_all;
			for(i1=1,i=0;i<n_all;i++,i1<<=1)	{
				a=all_set[i][ids];
				j=0;
				j_1=1;
				while(a) {
					if((a&1)&&(j<=i || !(all_set[j][ids]&i1))) {
						cm=i1|j_1;
						for(m=0;m<nkids;m++) if(!(cm&ccm[m])) break;
						if(m==nkids) for(k1=0;k1<nmc;k1++) {
							k=m_set1[k1];
							l=m_set2[k1];
							if(((tt_all[k]&cm)||(tt_all[l]&cm))&&((t_all1[k]&cm)||(t_all1[l]&cm))&&((t_all2[k]&cm)||(t_all2[l]&cm))) {
								temp_set[k]|=cm;
								temp_set[l]|=cm;
								id_set[0][i]|=j_1;
								id_set[0][j]|=i1;
								id_set[1][k]|=1<<l;
								id_set[1][l]|=1<<k;
								nc++;
							}
						}
					}
					a>>=1;
					j++;
					j_1<<=1;
				}
			}
			break;
		 default:
			for(i1=1,i=0;i<n_all;i++,i1<<=1)	{
				a=all_set[i][ids];
				j=0;
				j_1=1;
				while(a) {
					if((a&1)&&(j<=i || !(all_set[j][ids]&i1))) {
						cm=i1|j_1;
						for(m=0;m<nkids;m++) if(!(cm&ccm[m])) break;
						if(m==nkids) for(k1=0;k1<nmc;k1++) {
							k=m_set1[k1];
							l=m_set2[k1];
							t_all1=tt_all;
							for(m=0;m<nkids;m++,t_all1+=n_all) if(!((t_all1[k]&cm)||(t_all1[l]&cm))) break;
							if(m==nkids) {
								temp_set[k]|=cm;
								temp_set[l]|=cm;
								id_set[0][i]|=j_1;
								id_set[0][j]|=i1;
								id_set[1][k]|=1<<l;
								id_set[1][l]|=1<<k;
								nc++;
							}
						}
					}
					a>>=1;
					j++;
					j_1<<=1;
				}
			}
		}
	} else if(linktype==LINK_X) {
		for(i=0;i<n_all;i++) if(all_set[i][ids]) {
			for(k=0;k<n_all;k++)	{
				b=all_set[k][idd];
				if(b) for(l=0;l<n_all;l++) if(b&(1<<l)) {
					if(l>k && (all_set[l][idd]&(1<<k))) continue;
					cm=(1<<i);
					fg=0;
					for(m=0;m<family[fam].nkids;m++)	{
						kid=family[fam].kids[m];
						if(id_array[kid].flag&IS_PRUNED) continue;
						cm1=(id_array[kid].sex==1?1:cm);
						kid=ped_recode1[kid]-1-id;
						if(!((all_set[k][kid]&cm1)||(all_set[l][kid]&cm1))) {
							fg=1;
							break;
						}
					}
					if(fg) continue;
					temp_set[k]|=cm;
					temp_set[l]|=cm;
					id_set[0][i]|=1;
					id_set[1][k]|=1<<l;
					id_set[1][l]|=1<<k;
					nc++;
				}
			}
		}
	} else if(linktype==LINK_Y) {
		a=all_set[0][ids];
		for(i=0;i<n_all;i++) if(a&(1<<i)) {
			fg=0;
			cm=1<<i;
			for(m=0;m<family[fam].nkids;m++)	{
				kid=family[fam].kids[m];
				if((id_array[kid].flag&IS_PRUNED) || id_array[kid].sex==2) continue;
				kid=ped_recode1[kid]-1-id;
				if(!(all_set[0][kid]&cm)) {
					fg=1;
					break;
				}
			}
			if(!fg) {
				temp_set[0]=cm;
				nc=1;
			}
		}
	}
	if(linktype!=LINK_Y)	{
		for(k=l=j=0;j<n_all;j++) {
			if(all_set[j][ids]&id_set[0][j]) k++;
			if(all_set[j][idd]&id_set[1][j]) l++;
			if(k&&l) break;
		}
	} else j=0;
	if(!nc || j==n_all) {
		if(!silent_flag) {
			(void)fprintf(stderr,"\nDoFamily(): Inconsistent family data for locus %s\n",marker_name);
			PrintFamily(fam,locus,n_all,linktype,0);
		}
		return 1;
	}
	if(linktype!=LINK_Y) for(j=0;j<n_all;j++) {
		a=all_set[j][ids];
		all_set[j][ids]&=id_set[0][j];
		if(a!=all_set[j][ids]) change=1;
		a=all_set[j][idd];
		all_set[j][idd]&=id_set[1][j];
		if(a!=all_set[j][idd]) change=1;
	}
	for(m=0;m<family[fam].nkids;m++)	{
		kid=family[fam].kids[m];
		if(id_array[kid].flag&IS_PRUNED || (linktype==LINK_Y && id_array[kid].sex==2)) continue;
		l=(linktype==LINK_Y?1:n_all);
		k=ped_recode1[kid]-1-id;
		for(j=0;j<l;j++) {
			if(linktype==LINK_X && id_array[kid].sex==1) cm=temp_set[j]?1:0; 
			else cm=temp_set[j];
			if(all_set[j][k]&cm) break;
		}
		if(j==l)	{
			(void)fprintf(stderr,"\nDoFamily(): (Internal error?) Inconsistent family data for locus %s\n",marker_name);
			PrintFamily(fam,locus,n_all,linktype,0);
			ABT_FUNC(AbMsg);
		}
		for(j=0;j<l;j++) {
			a=all_set[j][k];
			if(linktype==LINK_X && id_array[kid].sex==1) cm=temp_set[j]?1:0; 
			else cm=temp_set[j];
			all_set[j][k]&=cm;
			if(a!=all_set[j][k]) change=1;
		}
	}
	if(change) FlagFam(fam);
	famflag[fam]&=~1;
	return 0;
}

static void calc_order(const int comp,const int n_fam,const int *famlist,const int linktype)
{
	int i,j,fflag,fam,kid,ids,idd;
	
	for(j=0;j<comp_size[comp];j++) {
		i=perm[id+j];
		id_array[i].order=0;
	}
	for(i=0;i<n_fam;i++) {
		fam=famlist[i];
		ids=family[fam].sire;
		idd=family[fam].dam;
		if(ids) {	
			fflag=0;
			if(linktype!=LINK_Y) id_array[idd-1].order++;
			for(j=0;j<family[fam].nkids;j++)	{
				kid=family[fam].kids[j];
				if(id_array[kid].flag&IS_PRUNED) continue;
				if(linktype==LINK_Y) {
					if(id_array[kid].sex==1) {
						id_array[kid].order++;
						fflag=1;
					}
				} else {
					id_array[kid].order++;
					if(id_array[kid].sex==2 || linktype==LINK_AUTO) fflag=1;
				}
			}
			id_array[ids-1].order+=fflag;
		} else {
			kid=family[fam].kids[0];
			if(id_array[kid].flag&IS_PRUNED) continue;
			if(linktype!=LINK_Y || id_array[kid].sex==1) id_array[kid].order++;
		}
	}
	for(j=0;j<comp_size[comp];j++) {
		i=perm[id+j];
		if(id_array[i].flag&IS_FIXED) id_array[i].order=1;
	} 
}

static void add_to_involved(const int i,int *n,int flag)
{
	int j;
	
	for(j=0;j<(*n);j++) if(i==involved[j]) break;
	if(j==(*n))	{
		rf_flag[j]=0;
		involved[(*n)++]=i;
	}
	if(flag) rf_flag[j]=1;
}

static int count_ops(const int i1,const int flag,const int linktype)
{
	int i,mflag,j,k,l,m,kid,fam,n_inv=1,sex;
		
	if(i1<0) {
		mflag=1;
		i=-1-i1; 
	} else {
		mflag=0;
		i=i1-1;
	}
	involved[0]=i1;
	rf_flag[0]=0;
	sex=id_array[i].sex;
	if(sex<0 || sex>2) ABT_FUNC("Internal error - illegal sex\n");
	if(linktype!=LINK_AUTO && !sex) ABT_FUNC("Internal error - unknown sex with sex-linked locus\n");
	if(linktype==LINK_Y) {
		if(sex==1) j=1;
		else j=0;
	} else if(sex==2 || linktype==LINK_AUTO) j=2;
	else j=1;
	if((id_array[i].flag&HAP_JNT)||(j==2&&(id_array[i].flag&HAP_DAT))) {
		rf_flag[n_inv]=0;
		involved[n_inv++]= -i1;
	}
	if(mflag) { /* Put in parents */
		if(id_array[i].flag&HAP_P) {
			if(linktype==LINK_AUTO) {
				add_to_involved(id_array[i].sire,&n_inv,0);
				add_to_involved(-id_array[i].sire,&n_inv,0);
			} else if(linktype==LINK_X) add_to_involved(id_array[i].sire,&n_inv,0);
			else add_to_involved(-id_array[i].sire,&n_inv,0);
			if(flag) id_array[i].flag|=HAD_P;
		}
	} else {
		if(id_array[i].flag&HAP_M)	{
			add_to_involved(id_array[i].dam,&n_inv,0);
			add_to_involved(-id_array[i].dam,&n_inv,0);
			if(flag) id_array[i].flag|=HAD_M;
		}
	}
	for(j=0;j<id_array[i].nfam;j++) { /* And offspring */
		fam=id_array[i].famlist[j];
		if(!sex) ABT_FUNC("Internal Error - unknown sex for parent\n");
		for(k=0;k<family[fam].nkids;k++)	{
			kid=family[fam].kids[k];
			if(sex==1)	{
				if(id_array[kid].flag&HAP_P) {
					add_to_involved(-(kid+1),&n_inv,0);
					if(flag) id_array[kid].flag^=(HAP_P|HAD_P);
				}
			} else {
				if(id_array[kid].flag&HAP_M) {
					add_to_involved(kid+1,&n_inv,0);
					if(flag) id_array[kid].flag^=(HAP_M|HAD_M);
				}
			}
		}
	}
	/* Collect R-Functions referencing this node */
	for(j=0;j<n_rfuncs;j++) if(!r_func[j].flag) {
		for(k=0;k<r_func[j].n_ind;k++) if(r_func[j].id_list[k]==i1) break;
		if(k<r_func[j].n_ind) {
			for(k=0;k<r_func[j].n_ind;k++) {
				l=r_func[j].id_list[k];
				add_to_involved(l,&n_inv,1);
			}
			if(flag) r_func[j].flag=1;
		}
	}
	/* Check for other R-Functions that can be included without increasing involved count */
	for(j=0;j<n_rfuncs;j++) if(!r_func[j].flag) {
		for(k=0;k<r_func[j].n_ind;k++) {
			m=r_func[j].id_list[k];
			if(m==i1) break;
			for(l=0;l<n_inv;l++) if(m==involved[l]) break;
			if(l==n_inv) break;
		}
		if(flag && k==r_func[j].n_ind) r_func[j].flag=1;
	}
	if(flag) {
		if(mflag) id_array[i].flag&=~(HAP_P|HAP_PAT|HAP_JNT|HAP_DAT);
		else id_array[i].flag&=~(HAP_M|HAP_MAT|HAP_JNT|HAP_DAT);
	} else if(n_prev_inv) {
		for(j=0;j<n_inv;j++)	{
			l=involved[j];
			for(k=0;k<n_prev_inv;k++) if(l==prev_inv[k]) break;
			if(k==n_prev_inv) break;
		}
		if(j==n_inv) return -1;
	}
	return n_inv-1;
}

static int check_haps(const int i,const int linktype)
{
	int fgs=0;
	
	if(id_array[i].flag&PEELED) return fgs;
	if(linktype==LINK_Y) {
		if(id_array[i].sex==1) fgs|=HAP_PAT;
	} else if(id_array[i].sex==2 || linktype==LINK_AUTO) fgs|=HAP_MAT|HAP_PAT;
	else fgs|=HAP_MAT;
	if(id_array[i].flag&WAS_PIVOT) fgs|=HAP_DAT;
	if(id_array[i].flag&HAS_DATA) fgs|=HAP_DAT;
	if((fgs&(HAP_DAT|HAP_MAT|HAP_PAT))==(HAP_DAT|HAP_MAT|HAP_PAT)) fgs|=HAP_JNT;
	return fgs;
}

static void set_flags(const int n_fam,const int *famlist,const int comp,const int linktype)
{
	int i,j,fm,fp,kid,fam,ids,idd;
	
	for(j=0;j<comp_size[comp];j++) {
		i=perm[id+j];
		id_array[i].flag&=STABLE_FLAGS;
	}
	for(i=0;i<n_fam;i++) {
		fam=famlist[i];
		ids=family[fam].sire;
		if(ids) {
			if(id_array[ids-1].flag&PEELED) ids=fp=0;
			else fp=check_haps(ids-1,linktype);
			idd=family[fam].dam;
			if(id_array[idd-1].flag&PEELED) idd=fm=0;
			else fm=check_haps(idd-1,linktype);
		} else idd=fm=fp=0;
		for(j=0;j<family[fam].nkids;j++)	{
			kid=family[fam].kids[j];
			if(id_array[kid].flag&(IS_PRUNED|PEELED)) continue;
			id_array[kid].flag|=check_haps(kid,linktype);
			if(linktype==LINK_Y) { 
				if(id_array[kid].sex==1 && ids) {
					id_array[kid].flag|=HAP_P;
					fp|=HAP_PAT;
				}
			} else if(id_array[kid].sex==2 || linktype==LINK_AUTO) {
				if(ids) {
					id_array[kid].flag|=HAP_P;
					if(linktype==LINK_X) fp|=HAP_MAT;
					else fp|=HAP_MAT|HAP_PAT|HAP_JNT;
				}
				if(idd) {
					id_array[kid].flag|=HAP_M;
					fm|=HAP_MAT|HAP_PAT|HAP_JNT;
				}
			} else id_array[kid].flag|=HAP_M;
		}
		if(ids) id_array[ids-1].flag|=fp;
		if(idd) id_array[idd-1].flag|=fm;
	}
}

static int logical_jntpeel(int n_genes,int *order,int *g_perm,const int n_all,struct Peelseq_Head *pp,const int linktype)
{
	int x,i,j,k,k1= -1,k2,k3,k4,l,pivot,n_ops,n_rf,err=0;
	int n_inv=0,n_peel,n_bits;
	struct Complex_Element *element;

#ifdef TRACE_PEEL
 	if(CHK_PEEL(TRACE_LEVEL1)) (void)printf("In logical_jnt_peel(%d,%p,%p,%d,%p,%d)\n",n_genes,(void *)order,(void *)perm,n_all,(void *)pp,linktype);
#endif
	n_bits=num_bits(n_all);
	for(x=0;x<=n_genes;x++) {
		if(x==n_genes) pivot=n_ops=0;
		else {
			pivot=order[x]+1;
			i=g_perm[pivot-1];
			n_ops=count_ops(i,0,linktype);
		}
		if(n_inv && n_ops<0) {
			if(n_bits*(n_inv+1)>(int)LK_LONG_BIT) {
#ifdef TRACE_PEEL
				if(CHK_PEEL(TRACE_LEVEL2)) 
				(void)fputs("Splitting op as n_peel getting too big\n",stdout);
#endif
				n_ops=0;
			}
		}
		if(n_inv && n_ops>=0) {
			n_peel=n_inv;
			for(k=0;k<n_prev_inv;k++) {
				j=prev_inv[k];
				true_involved[n_inv++]=j;
			}
			if(!(element=malloc(sizeof(struct Complex_Element)))) ABT_FUNC(MMsg);
			pp->type=PEEL_COMPLEX;
			pp->ptr.complex=element;
			pp= &element->next;
			pp->type=0;
			element->n_peel=n_peel;
			element->n_involved=n_inv;
			for(n_rf=k=0;k<n_rfuncs;k++) if(r_func[k].flag==1) n_rf++;
			element->n_rfuncs=n_rf;
			if(!(element->involved=malloc(sizeof(int)*(n_inv*2+n_rf)))) ABT_FUNC(MMsg);
			element->flags=element->involved+n_inv;
			element->index=element->flags+n_inv;
			if(n_peel==n_inv)	{
				free(r_func[k1].id_list);
				n_rfuncs--;
				k1= -1;
			}
 			element->out_index=k1;
			for(k=0;k<n_inv;k++) element->flags[k]=0;
			for(k=0;k<n_inv;k++) {
				l=true_involved[k];
				if(l>0) {
					element->flags[k]|=id_array[l-1].flag&HAD_M;
					id_array[l-1].flag&=~HAD_M;
				} else {
					element->flags[k]|=id_array[-1-l].flag&HAD_P;
					id_array[-l-1].flag&=~HAD_P;
				}
			}
			for(j=0;j<n_inv;j++) element->involved[j]=true_involved[j];
			/* Make list of R-Functions involved in operation */
 			for(n_rf=k=0;k<n_rfuncs;k++) if(r_func[k].flag==1) {
				for(k1=0;k1<r_func[k].n_ind;k1++) {
					for(j=0;j<n_inv;j++) if(r_func[k].id_list[k1]==true_involved[j]) break;
					if(j==n_inv) ABT_FUNC("Internal error - allele not found.\n");
					r_func[k].id_list[k1]=j;
					element->flags[j]|=IN_RF;
				}
				element->index[n_rf++]=k;
				r_func[k].flag=2;
			}
			for(k=0;k<n_inv-1;k++) {
				l=true_involved[k];
				for(j=k+1;j<n_inv;j++) if(true_involved[j]== -l) {
					for(k2=0;k2<n_rfuncs;k2++) if(r_func[k2].flag==1) {/* Check if both alleles are in *same* R-Func */
						for(k3=k4=0;k3<r_func[k2].n_ind;k3++) {
							if(l==r_func[k2].id_list[k3]) k4++;
							if(-l==r_func[k2].id_list[k3]) k4++;
							if(k4==2) break;
						}
						if(k4==2) break;
					}
					if(k2==n_rfuncs) {
						element->flags[j]|=HAP_JNT;
						element->flags[k]|=HAP_JNT;
					}
					break;
				}
			}
			if(!no_peel_flag) {
				if(do_peel_op(element,r_func,n_all,id,all_set,req_set)) {
					if(silent_flag) {
						k1=abs(element->involved[0])-1;
						err=id_array[k1].family+1;
					} else {
						fputs("Zero probability during peeling operation with genes:\n",stderr);
						for(k=0;k<element->n_involved;k++) {
							fputs("  ",stderr);
							print_orig_allele_id(stderr,element->involved[k]);
							fputc('\n',stderr);
						}
						ABT_FUNC("Aborting\n");
					}
				}
			} else {
				k1=element->out_index;
				if(k1>=0) for(k=0;k<r_func[k1].n_ind;k++)
					  r_func[k1].id_list[k]=true_involved[n_peel+k];
			}
			for(k=0;k<element->n_rfuncs;k++) {
				k1=element->index[k];
				if(r_func[k1].index) free(r_func[k1].index);
			}
			n_inv=0;
			n_prev_inv=0;
		}
		if(!pivot || err) break;
		j=pivot-1;
		pivot=g_perm[j];
		n_ops=count_ops(pivot,1,linktype);
		if(!n_inv) {
			if(n_rfuncs>=r_func_size) {
				r_func_size<<=1;
				if(!(r_func=realloc(r_func,sizeof(struct R_Func)*r_func_size))) ABT_FUNC(MMsg);
			}
			k1=n_rfuncs++;
			if(!(r_func[k1].id_list=malloc(sizeof(int)*(1+n_ops)))) ABT_FUNC(MMsg);
			r_func[k1].n_ind=n_ops;
			r_func[k1].flag=0;
			r_func[k1].n_terms=0;
			r_func[k1].index=0;
			for(k=1;k<=n_ops;k++) r_func[k1].id_list[k-1]=involved[k];
		} else if(n_inv) {
			for(k=0;k<r_func[k1].n_ind;k++) if(r_func[k1].id_list[k]==pivot) {
				r_func[k1].id_list[k]=r_func[k1].id_list[--r_func[k1].n_ind];
				break;
			}
			r_func[k1].flag=0;
		}
		for(k=1;k<=n_ops;k++) prev_inv[k-1]=involved[k];
		n_prev_inv=n_ops;	
		true_involved[n_inv++]=pivot;
	}
	if(!no_peel_flag && silent_flag!=1) (void)printf("No. non-zero terms = %d, non-zero combinations = %d\n",total_terms,total_comb);
	for(k1=i=0;i<n_rfuncs;i++) if(!r_func[i].flag) {
		if(err) {
			if(r_func[i].index) free(r_func[i].index);
		} else {
			(void)fputs("Unused R-Function on: ",stderr);
			for(j=0;j<r_func[i].n_ind;j++) {
				k=r_func[i].id_list[j];
				print_orig_allele_id(stderr,k);
				(void)fputc(' ',stderr);
			}
			(void)fputc('\n',stderr);
			k1=1;
		}
	}
	if(k1) ABT_FUNC(AbMsg);
	return err;
}

static int *assemble_matrix(int size,int *g_perm,int *trans,int k2,const int linktype)
{
	int i,x,i1,j,k,l,kid,fam,n_inv,ptr,ng,sex,*mat,mat_size;

	if(!size) return 0;
	mat_size=size*2+1;
	if(!(mat=malloc(sizeof(int)*mat_size))) ABT_FUNC(MMsg);
	ptr=size+1;
	for(x=0;x<size;x++) {
		i1=g_perm[x];
		i=abs(i1)-1;
		n_inv=0;
		sex=id_array[i].sex;
		if(sex<0 || sex>2) ABT_FUNC("Internal error - illegal sex\n");
		if(linktype!=LINK_AUTO && !sex) ABT_FUNC("Internal error - unknown sex with sex-linked locus\n");
		add_to_involved(i1,&n_inv,0);
		/* How many genes to add for this individual (may depend on sex) */
		if(linktype==LINK_Y) {
			if(sex==1) ng=1;
			else ng=0;
		} else if(sex==2 || linktype==LINK_AUTO) ng=2;
		else ng=1;
		if((id_array[i].flag&HAP_JNT)||(ng==2 &&(id_array[i].flag&HAP_DAT))) add_to_involved(-i1,&n_inv,0);
		if(i1<0) { /* Put in parents */
			if(id_array[i].flag&HAP_P) {
				if(linktype==LINK_AUTO) {
					/* Add in sire's alleles */
					add_to_involved(id_array[i].sire,&n_inv,0);
					add_to_involved(-id_array[i].sire,&n_inv,0);
				} else if(linktype==LINK_X) {
					if(sex==1) ABT_FUNC("Internal error - father-male offspring link for X-linked locus\n");
					/* Add in sire's maternal allele */
					add_to_involved(id_array[i].sire,&n_inv,0);
				} else { /* Y-Linked */
					if(sex==2) ABT_FUNC("Internal error - father-female offspring link for Y-linked locus\n");
					/* Add in sire's paternal allele */
					add_to_involved(-id_array[i].sire,&n_inv,0);
				}
			}
		} else {
			if(id_array[i].flag&HAP_M)	{
				if(linktype==LINK_Y) ABT_FUNC("Internal error - mother-offspring link for Y-linked locus\n");
				else {
					/* Add in dam's alleles */
					add_to_involved(id_array[i].dam,&n_inv,0);
					add_to_involved(-id_array[i].dam,&n_inv,0);
				}
			}
		}
		for(j=0;j<id_array[i].nfam;j++) {
			fam=id_array[i].famlist[j];
			if(!sex) ABT_FUNC("Internal error - unknown sex for parent\n");
			for(k=0;k<family[fam].nkids;k++)	{
				kid=family[fam].kids[k];
				if(sex==1) {
					if(id_array[kid].flag&HAP_P) {
						if(linktype==LINK_X && id_array[kid].sex==1) ABT_FUNC("Internal error - father-male offspring link for X-linked locus\n");
						if(linktype==LINK_Y && id_array[kid].sex==2)  ABT_FUNC("Internal error - father-female offspring link for Y-linked locus\n");
						add_to_involved(-(kid+1),&n_inv,0);
					}
				} else {
					if(id_array[kid].flag&HAP_M) {
						if(linktype==LINK_Y) ABT_FUNC("Internal error - Mother-offspring link for Y-linked locus\n");
						add_to_involved(kid+1,&n_inv,0);
					}
				}
			}
		}
		for(j=0;j<n_rfuncs;j++) if(!r_func[j].flag) {
			for(k=0;k<r_func[j].n_ind;k++) if(r_func[j].id_list[k]==i1) break;
			if(k<r_func[j].n_ind) {
				for(k=0;k<r_func[j].n_ind;k++) {
					l=r_func[j].id_list[k];
					add_to_involved(l,&n_inv,0);
				}
			}
		}
		/* Add to matrix */
		/* Count no. entries for this row */
		for(j=0,k=1;k<n_inv;k++) {
			if(trans[k2+involved[k]]<0) {
				  ABT_FUNC("Internal error - illegal index\n");
			}
			involved[k]=trans[k2+involved[k]];
			if(involved[k]<x) j++;
		}
		/* Check space */
		if(mat_size<ptr+j) {
			do mat_size*=1.5; while(mat_size<ptr+j);
			if(!(mat=realloc(mat,sizeof(int)*mat_size))) ABT_FUNC(MMsg);
		}
		mat[x]=ptr;
		for(k=1;k<n_inv;k++)	if(involved[k]<x) mat[ptr++]=involved[k];
	}
	mat[x]=ptr;
	return mat;
}
	
static int *min_degree(int *g_perm,int size,const int linktype)
{
	int x,i1,k1,k2,*trans;
	int *mat,*order;
	
	k2=INT_MAX;
	k1=INT_MIN;
	for(x=0;x<size;x++) {
		i1=g_perm[x];
		if(i1<k2) k2=i1;
		if(i1>k1) k1=i1;
	}
	k2= -k2;
	k1+=k2+1;
	if(!(trans=malloc(sizeof(int)*k1))) ABT_FUNC(MMsg);
	for(x=0;x<k1;x++) trans[x]= -1;
 	for(x=0;x<size;x++) {
		i1=g_perm[x]+k2;
		trans[i1]=x;
	}
	mat=assemble_matrix(size,g_perm,trans,k2,linktype);
	free(trans);
	if(!mat) ABT_FUNC("Internal error - assemble_matrix() returned a zero pointer\n");
	if(!(order=malloc(sizeof(int)*size))) ABT_FUNC(MMsg);
	min_deg(size,mat,order,0,0);
	free(mat);
	return order;
}

static int find_sequence(const int n_fam,const int *famlist,const int n_all,struct Peelseq_Head *pp,const int locus,const int comp,const int linktype)
{
	int i,j,n_genes=0,old_rfuncs,*bk_flags=0;
	int *g_perm,*order=0,*order1;
	struct Marker *mark;
	
#ifdef TRACE_PEEL
	if(CHK_PEEL(TRACE_LEVEL1)) (void)printf("In find_sequence(%d,%p,%d,%p,%d,%d,%d)\n",n_fam,(void *)famlist,n_all,(void *)pp,locus,comp,linktype);
#endif
	old_rfuncs=n_rfuncs;
	if(n_rfuncs) {
		if(!(bk_flags=malloc(sizeof(int)*n_rfuncs))) ABT_FUNC(MMsg);
		for(i=0;i<n_rfuncs;i++) bk_flags[i]=r_func[i].flag;
	}
	set_flags(n_fam,famlist,comp,linktype);
	for(j=0;j<comp_size[comp];j++) {
		i=perm[id+j];
		if(id_array[i].flag&HAP_MAT) n_genes++;
		if(id_array[i].flag&HAP_PAT) n_genes++;
	}
	if(!n_genes) return 0;
#ifdef TRACE_PEEL
	if(CHK_PEEL(TRACE_LEVEL1)) (void)printf("-> n_genes = %d\n",n_genes);
#endif
	if(!(involved=malloc(sizeof(int)*n_genes*6))) ABT_FUNC(MMsg);
	n_prev_inv=0;
	prev_inv=involved+n_genes;
	true_involved=prev_inv+n_genes;
	rf_flag=true_involved+n_genes;
	g_perm=rf_flag+n_genes;
	order1=g_perm+n_genes;
	n_genes=0;
	for(j=0;j<comp_size[comp];j++) {
		i=perm[id+j];
		if(id_array[i].flag&HAP_MAT) {
			g_perm[n_genes++]=i+1;
		}
		if(id_array[i].flag&HAP_PAT) {
			g_perm[n_genes++]= -(i+1);
		}
	}
	if(locus==n_markers) mark=traitlocus;
	else mark=markers+locus;
	order=min_degree(g_perm,n_genes,linktype);
	for(j=old_rfuncs;j<n_rfuncs;j++) free(r_func[j].id_list);
	n_rfuncs=old_rfuncs;
	set_flags(n_fam,famlist,comp,linktype);
	if(n_rfuncs) {
		for(j=0;j<n_rfuncs;j++) r_func[j].flag=bk_flags[j];
		free(bk_flags);
	}
	i=logical_jntpeel(n_genes,order,g_perm,n_all,pp,linktype);
	if(order) free(order);
	free(involved);
	return i;
}

static void fill_rf(int pivot,int n_all,int n_bits,struct Peelseq_Head *pp)
{
	int k,k1,k2,k3,piv1;
	lk_ulong a;
	
	if(n_rfuncs>=r_func_size) {
		r_func_size<<=1;
		if(!(r_func=realloc(r_func,sizeof(struct R_Func)*r_func_size))) ABT_FUNC(MMsg);
	}
	k=n_rfuncs++;
	if(!(r_func[k].id_list=malloc(sizeof(int)*2))) ABT_FUNC(MMsg);
	r_func[k].id_list[0]=pivot;
	r_func[k].id_list[1]= -pivot;
	r_func[k].n_ind=2;
	r_func[k].flag=0;
	r_func[k].mask=0;
	r_func[k].peel_elem=pp;
	piv1=ped_recode1[pivot-1]-1-id;
	for(k1=k2=0;k2<n_all;k2++) {
		a=all_set[k2][piv1];
		if(a) for(k3=0;k3<n_all;k3++) if(a&(1<<k3)) k1++;
	}
	r_func[k].n_terms=k1;
	if(!(r_func[k].index=malloc(k1*sizeof(lk_ulong)))) ABT_FUNC(MMsg);
	for(k1=k2=0;k2<n_all;k2++) {
		a=all_set[k2][piv1];
		if(a) for(k3=0;k3<n_all;k3++) if(a&(1<<k3)) {
			r_func[k].index[k1++]=(lk_ulong)((k3<<n_bits)|k2);
		}
	}
}

static char *get_errfile(const char *s,int fg)
{
	char *fname1=0,*fname2;
	size_t sz;
	
	if(ErrorDir) {
		sz=strlen(ErrorDir)+strlen(s)+(fg?7:6);
		if((fname1=malloc(sz)))
		  (void)snprintf(fname1,sz,"%s/%s.er%s",ErrorDir,s,fg?"r1":"r");
	} else {
		sz=strlen(s)+(fg?6:5);
		if((fname2=malloc(sz))) {
			(void)snprintf(fname2,sz,"%s.er%s",s,fg?"r1":"r");
			fname1=add_file_dir(fname2);
			free(fname2);
		}
	}
	return fname1;
}

static int logical_peel(const int n_fam,int *famlist,const int n_all,const int locus,const int comp,const int linktype,FILE *fptr)
{
	int i,j,k,k1,k2,k3,k4,pivot,fam,kid,ids,idd,nf,n_bits,nfx,nunfx,nfx1,nunfx1,flag=0;
	lk_ulong a,b,c;
	struct Peelseq_Head *pp,*pp1;
	struct Simple_Element *element;
	struct Marker *mark;

#ifdef TRACE_PEEL
	if(CHK_PEEL(TRACE_LEVEL1)) (void)printf("In logical_peel(%d,%p,%d,%d,%d,%d)\n",n_fam,(void *)famlist,n_all,locus,comp,linktype);
#endif
	n_bits=num_bits(n_all);
	peelseq_head.type=0;
	pp= &peelseq_head;
	r_func_size=n_fam+1;
	if(!(r_func=malloc(sizeof(struct R_Func)*r_func_size))) ABT_FUNC(MMsg);
	n_rfuncs=0;
	for(k=j=0;j<comp_size[comp];j++) {
		i=perm[id+j];
		if(id_array[i].flag&IS_SINGLETON) k++;
	}
	if(k) {
#ifdef TRACE_PEEL
		if(CHK_PEEL(TRACE_LEVEL2)) (void)printf("\tFound %d singletons\n",k);
#endif
		if(!(element=malloc(sizeof(struct Simple_Element)))) ABT_FUNC(MMsg);
		pp->type=PEEL_SIMPLE;
		pp->ptr.simple=element;
		pp= &element->next;
		pp->type=0;
		element->sire=element->dam=element->pivot=0;
		element->out_index= -1;
		if(!(element->off=malloc(sizeof(int)*k))) ABT_FUNC(MMsg);
		for(k=j=0;j<comp_size[comp];j++)	{
			i=perm[id+j];
			if(id_array[i].flag&IS_SINGLETON) element->off[k++]=i+1;
		}
		element->n_off=k;
	}
	nfx=nunfx=nfx1=nunfx1=0;
	for(j=0;j<comp_size[comp];j++) {
		i=perm[id+j];
		id_array[i].rf_idx=-1;
		if(id_array[i].flag&(IS_PRUNED|IS_SINGLETON)) continue;
		id_array[i].ngens=id_array[i].nhaps[0]=id_array[i].nhaps[1]=0;
		if(linktype==LINK_Y && id_array[i].sex==2) continue;
		k1=k3=k4=0;
		for(b=0,k=0;k<n_all;k++) {
			a=all_set[k][j];
			if(!a) continue;
			k1++;
			b|=a;
			if(a&(1<<k)) k4++;
			while(a) {
				if(a&1) k3++;
				a>>=1;
			}
		}
		k2=0;
		id_array[i].nhaps[X_MAT]=k1;
		req_set[2][j]=b;
		while(b) {
			if(b&1) k2++;
			b>>=1;
		}
		if(k1==1 && k2==1) {
			id_array[i].flag|=IS_FIXED;
		}
		id_array[i].nhaps[X_PAT]=k2;
		id_array[i].ngens=k3;
		id_array[i].ngens1=k4;
		id_array[i].sg[X_MAT]=id_array[i].sg[X_PAT]=-1;
		if(k1==1) nfx++;
		else nunfx++;
		if(k2==1) nfx++;
		else nunfx++;
		if((ids=id_array[i].sire) && !(id_array[ids-1].flag&IS_PRUNED)) {
			ids=ped_recode1[ids-1]-1-id;
			a=req_set[X_PAT][j];
			b=1;
			k1=k2=0;
			for(k=0;k<n_all;k++) {
				if(req_set[2][j]&b) {
					if(a&b) b=a;
					if(all_set[k][ids]) k1++;
					c=req_set[2][ids];
					if(c&b) k2++;
				}
				b<<=1;
			}
			if(k1&&k2) {
				nunfx1++;
				id_array[i].sg[X_PAT]=2;
			} else if(k1||k2) {
				nfx1++;
				id_array[i].sg[X_PAT]=k1?0:1;
			} else ABT_FUNC("Error - illegal config!\n");
			ids=ped_recode1[id_array[i].sire-1];			
		} else ids=0;
		if((idd=id_array[i].dam) && !(id_array[idd-1].flag&IS_PRUNED)) {
			idd=ped_recode1[idd-1]-1-id;
			a=req_set[X_MAT][j];
			k1=k2=0;
			for(k=0;k<n_all;k++) if(all_set[k][j]) {
				b=1<<k;
				if(a&b) b=a;
				if(all_set[k][idd]) k1++;
				c=req_set[2][idd];
				if(c&b) k2++;
			}
			if(k1&&k2) {
				nunfx1++;
				id_array[i].sg[X_MAT]=2;
			} else if(k1||k2) {
				nfx1++;
				id_array[i].sg[X_MAT]=k1?0:1;
			} else {
				ABT_FUNC("Error - illegal config!\n");
			}
			idd=ped_recode1[id_array[i].dam-1];			
		} else idd=0;
		if(fptr) {
			if(!flag++) (void)fprintf(fptr,"No. alleles in component: %d\n",n_all);
/*			(void)fprintf(fptr,"%d %d %d %d  %d %d %d %d %d ",comp,ped_recode1[i],ids,idd,
							  id_array[i].nhaps[X_MAT],id_array[i].nhaps[X_PAT],
							  id_array[i].sg[X_MAT],id_array[i].sg[X_PAT],id_array[i].ngens); */
			print_orig_id(fptr,i+1,0);
			fprintf(fptr," %d %d %d ",id_array[i].ngens,id_array[i].nhaps[0],id_array[i].nhaps[1]);
			
			(void)fprintf(fptr,"%lx %lx",req_set[X_MAT][j],req_set[X_PAT][j]);
			/*
#ifdef USE_LONGLONG
			(void)fprintf(fptr,"%llx %llx %llx ",req_set[X_MAT][j],req_set[X_PAT][j],req_set[2][j]);
#else
			(void)fprintf(fptr,"%lx %lx %lx ",req_set[X_MAT][j],req_set[X_PAT][j],req_set[2][j]);
#endif */
/*			for(k=0;k<n_all;k++) {
#ifdef USE_LONGLONG
				(void)fprintf(fptr,"%llx ",all_set[k][j]);
#else
				(void)fprintf(fptr,"%lx ",all_set[k][j]);
#endif 
			} */
			(void)fputc('\n',fptr); 
		} 
	} 
	calc_order(comp,n_fam,famlist,linktype);
	nf=n_fam;
	k1=0;
	if(((syst_var[PEEL_OPTION]&1) && locus<n_markers) 
		|| ((syst_var[PEEL_OPTION]&2) && locus==n_markers)) k1=1;
#ifdef TRACE_PEEL
	if(!k1 && CHK_PEEL(TRACE_LEVEL1) && nf) (void)printf("-> Beginning primary family peel (nf = %d)\n",nf);
#endif
	if(!k1) for(i=0;i<nf;i++) {
		fam=famlist[i];
		ids=family[fam].sire;
		idd=family[fam].dam;
		pivot=0;
		if(ids) {
			if(id_array[ids-1].order>1) pivot=ids;
			if(id_array[idd-1].order>1) {
				if(pivot) pivot= -2;
				else pivot=idd;
			}
		}
		for(j=0;j<family[fam].nkids;j++) {
			kid=family[fam].kids[j];
			if(id_array[kid].flag&IS_PRUNED) continue;
			if(pivot<0) {
				if(id_array[kid].ngens>1) break;
			} else if(id_array[kid].order>1)	{
				if(pivot) break;
				pivot=kid+1;
			}
		}
		if(j==family[fam].nkids) {
			if(pivot>=0 || !(syst_var[PEEL_OPTION]&4)) {
				if(!(element=malloc(sizeof(struct Simple_Element)))) ABT_FUNC(MMsg);
				pp1=pp;
				pp->type=PEEL_SIMPLE;
				pp->ptr.simple=element;
				pp= &element->next;
				pp->type=0;
				element->sire=ids;
				element->dam=idd;
				if(ids && ids!=pivot && pivot>=0) id_array[ids-1].flag|=PEELED;
				if(idd && idd!=pivot && pivot>=0) id_array[idd-1].flag|=PEELED;
				element->pivot=pivot;
				if((k=id_array[ids-1].rf_idx)>=0) {
					r_func[k].flag=2;
					free(r_func[k].index);
					id_array[ids-1].rf_idx=-1;
				}
				if((k=id_array[idd-1].rf_idx)>=0) {
					r_func[k].flag=2;
					free(r_func[k].index);
					id_array[idd-1].rf_idx=-1;
				}
				for(j=0;j<family[fam].nkids;j++) {
					kid=family[fam].kids[j]+1;
					if((k=id_array[kid-1].rf_idx)>=0) {
						r_func[k].flag=2;
						free(r_func[k].index);
						id_array[kid-1].rf_idx=-1;
					}
				}
#ifdef TRACE_PEEL
				if(CHK_PEEL(TRACE_LEVEL2)) {
					(void)fputs("--> Peeling family: ",stdout);
					if(family_id) {
						print_orig_family(stdout,family[fam].kids[0]+1,0);
						fputc(' ',stdout);
					}
					print_orig_id1(stdout,ids,0);
					(void)fputc(',',stdout);
					print_orig_id1(stdout,idd,0);
					(void)fputc(' ',stdout);
					for(j=k=0;j<family[fam].nkids;j++) {
						kid=family[fam].kids[j];
						if(id_array[kid].flag&IS_PRUNED) continue;
						(void)fputc(k++?',':'(',stdout);
						print_orig_id1(stdout,kid+1,0);
					}
					if(k) (void)fputc(')',stdout);
					(void)fputs(" --> ",stdout);
					if(pivot<0) {
						print_orig_id1(stdout,ids,0);
						(void)fputc(',',stdout);
						print_orig_id1(stdout,idd,0);
					} else if(!pivot) (void)fputc('.',stdout);
					else print_orig_id1(stdout,pivot,0);
					(void)fputc('\n',stdout);
				}
#endif
				if(pivot) {
					element->out_index=n_rfuncs;
					/* Peel to both parents separately - only if all children are fixed */
					if(pivot<0) {
						id_array[ids-1].flag|=WAS_PIVOT;
						id_array[idd-1].flag|=WAS_PIVOT;
						id_array[ids-1].order--;
						id_array[idd-1].order--;
						id_array[ids-1].rf_idx=n_rfuncs;
						fill_rf(ids,n_all,n_bits,pp);
						id_array[idd-1].rf_idx=n_rfuncs;
						fill_rf(idd,n_all,n_bits,pp);
					} else { /* Peel normally to a single pivot */
						id_array[pivot-1].flag|=WAS_PIVOT;
						id_array[pivot-1].order--;
						id_array[pivot-1].rf_idx=n_rfuncs;
						fill_rf(pivot,n_all,n_bits,pp);
					}
				} else element->out_index= -1;
				if(!(element->off=malloc(sizeof(int)*family[fam].nkids))) ABT_FUNC(MMsg);
				for(j=k=0;j<family[fam].nkids;j++) {
					kid=family[fam].kids[j];
					if(id_array[kid].flag&IS_PRUNED) continue;
					element->off[k++]=kid+1;
					if((kid+1)!=pivot) id_array[kid].flag|=PEELED;
				}
				element->n_off=k;
				famlist[i]=famlist[--nf];
				i= -1;
			}
		}
	}
	if(nf) {
/*		if(0) joint_peel(nf,famlist,n_all,pp,locus,comp,linktype); */
		return silent_flag?0:find_sequence(nf,famlist,n_all,pp,locus,comp,linktype); 
	} else if(!silent_flag) {
		if(locus<n_markers) mark=markers+locus;
		else mark=traitlocus;
	}
	return 0;
}

static int Check_Locus(const int locus,int component,const int linktype,FILE *lfptr)
{
	int i,j,k,k1,km,kp,fg,ids,idd,kid,n_all,ch[2],comp,all_trans[2],sex,n_comp1;
	int *famlist,*nfam,*all_flag,n_all1,n_fam,kid1,err=0,*tmp,*tmp1;
  	lk_ulong m1,a,b;
	struct Marker *mark;
	FILE *ferr=0;
	char *fname1=0;

#ifdef TRACE_PEEL
	if(CHK_PEEL(TRACE_LEVEL1)) (void)printf("In Check_Locus(%d,%s,%d,%d) - silent_flag = %d\n",locus,fname,component,linktype,silent_flag);
#endif
	rec_gen_flag=0;
	if(locus==n_markers) {
		if(!traitlocus) return 0;
		n_all=n_all_old=2;
	} else {
		n_all=n_all_old=markers[locus].element->n_levels;
		if(silent_flag!=1 && n_all) {
			if(!(tmp=calloc((size_t)(n_all*2),sizeof(int)))) ABT_FUNC(MMsg);
			tmp1=tmp+n_all;
			for(i=0;i<ped_size;i++) if(id_array[i].haplo[0]) {
				for(k=0;k<2;k++) {
					if((k1=id_array[i].haplo[k][locus])) tmp[k1-1]++;
				}
			}
			for(k1=k=0;k<n_all;k++) if(tmp[k]) k1++;
			if(k1<n_all) {
				for(k1=k=0;k<n_all;k++) {
					if(tmp[k]) {
						factor_recode[n_factors+locus][k1]=factor_recode[n_factors+locus][k];
						tmp1[k]=++k1;
					} else tmp1[k]= -1;
				}
				markers[locus].element->n_levels=n_all=k1;
				for(i=0;i<ped_size;i++) if(id_array[i].haplo[0]) {
					for(k=0;k<2;k++) if((k1=id_array[i].haplo[k][locus])) id_array[i].haplo[k][locus]=tmp1[k1-1];
				}
			}
			free(tmp);
		}
		if(!syst_var[NO_EXTRA_ALLELE]) n_all++;/* Add extra allele to allow for possible extra alleles in population */
	}
	if(n_all<2) return 0;
	if(!(all_flag=malloc(sizeof(int)*n_all))) ABT_FUNC(MMsg);
	if(!(temp_set=malloc(sizeof(lk_long)*n_all*3))) ABT_FUNC(MMsg);
	id_set[0]=temp_set+n_all;
	id_set[1]=temp_set+2*n_all;
	if(!(nfam=malloc((n_families*2+ped_size)*sizeof(int)))) ABT_FUNC(MMsg);
	famflag=nfam+ped_size;
	famlist=famflag+n_families;
	if(locus==n_markers)	{
		mark=traitlocus;
	} else {
		mark=markers+locus;
		if(!(mark->allele_trans=malloc(sizeof(void *)*n_comp))) ABT_FUNC(MMsg);
		if(!(mark->allele_trans[0]=malloc(sizeof(int)*n_comp*n_all))) ABT_FUNC(MMsg);
		for(i=1;i<n_comp;i++) mark->allele_trans[i]=mark->allele_trans[i-1]+n_all;
	}
	mark->order=0;
	no_peel_flag=(locus==n_markers);
	/* First thing to do is to prune pedigree based on data at this marker.
	 * Remove:
	 *   (a) Untyped individuals with no unpruned descendents
	 *   (b) Untyped founders with only 1 unpruned child
	 */
	for(i=0;i<n_families;i++) famflag[i]=0;
	for(j=0;j<pruned_ped_size;j++) id_array[perm[j]].flag=0;
	if(locus<n_markers) for(j=0;j<pruned_ped_size;j++)	{
		i=perm[j];
		if(id_array[i].haplo[0] && (id_array[i].haplo[0][locus] || id_array[i].haplo[1][locus]))
		  id_array[i].flag|=(HAS_DATA|HAS_GDATA);
		if(id_array[i].group) id_array[i].flag|=HAS_GDATA;
		nfam[i]=id_array[i].nfam;
	}
 	if(locus==n_markers || (markers[locus].element->type&ST_MODEL)) for(j=0;j<pruned_ped_size;j++) {
		i=perm[j];
		if(id_array[i].group) id_array[i].flag|=HAS_DATA;
		if(!id_array[i].flag && id_array[i].data) for(k=0;k<n_id_records;k++) {
			if((id_elements[k]->type&ST_TRAIT) && id_array[i].data[k].flag) {
				id_array[i].flag|=(HAS_DATA|HAS_GDATA);
				break;
			}
		}
		if(!id_array[i].flag && id_array[i].data1) for(k1=0;k1<id_array[i].nrec;k1++) {
			if(id_array[i].data1[k1]) for(k=0;k<n_nonid_records;k++)	{
				if((nonid_elements[k]->type&ST_TRAIT) && id_array[i].data1[k1][k].flag)	{
					id_array[i].flag|=(HAS_DATA|HAS_GDATA);
					break;
				}
			}
		}
		nfam[i]=id_array[i].nfam;
	}
	if(syst_var[PRUNE_OPTION]==2) do {
		for(fg=i=0;i<n_families;i++) if(!famflag[i])	{
			ids=family[i].sire;
			idd=family[i].dam;
			for(k=j=0;j<family[i].nkids;j++)	{
				kid=family[i].kids[j];
				if(id_array[kid].flag&IS_PRUNED) continue;
				if((id_array[kid].flag&HAS_GDATA) || nfam[kid]) k++;
				else {
					id_array[kid].flag|=IS_PRUNED;
					fg=1;
				}
			}
			if(k==1) {
				if(ids) {
					if(!(id_array[ids-1].flag&HAS_GDATA) && nfam[ids-1]<2) {
						k1=id_array[ids-1].family;
						if(!k1) k|=2;
						else if(famflag[k1-1]) k|=2;
					}
					if(!(id_array[idd-1].flag&HAS_GDATA) && nfam[idd-1]<2) {
						k1=id_array[idd-1].family;
						if(!k1) k|=4;
						else if(famflag[k1-1]) k|=4;
					}
					if(k==7) k=0;
				}
			}
			if(!k) {
				famflag[i]=fg=1;
				if(ids) {
					nfam[ids-1]--;
					if(!nfam[ids-1] && !(id_array[ids-1].flag&HAS_GDATA)) id_array[ids-1].flag|=IS_PRUNED;
					nfam[idd-1]--;
					if(!nfam[idd-1] && !(id_array[idd-1].flag&HAS_GDATA)) id_array[idd-1].flag|=IS_PRUNED;
				}
			}
		}
	} while(fg);
	/* mkflag[locus] indicates whether this individual has been pruned for
	 * this locus */
	for(k=j=0;j<pruned_ped_size;j++) {
		i=perm[j];
		if(id_array[i].flag&2) {
			k++;
			id_array[i].mkflag[locus]=1;
		} else {
			k1=id_array[i].family;
			if(k1 && famflag[k1-1]) k1=0;
			if(!k1 && !nfam[i]) id_array[i].flag|=IS_SINGLETON;
			id_array[i].mkflag[locus]=0;
			ids=id_array[i].sire;
			idd=id_array[i].dam;
			if(ids && (id_array[ids-1].mkflag[locus]^id_array[idd-1].mkflag[locus])) ABT_FUNC("Internal error - single parent pruned from family\n");
		}
	}
	id=comp=0;
	if(component>0) {
		for(;comp<component-1;comp++) id+=comp_size[comp];
		n_comp1=component;
	} else if(component<0) {
		component=-component;
		for(;comp<component-1;comp++) id+=comp_size[comp];
		n_comp1=n_comp;
	} else n_comp1=n_comp;
	/* Allele recoding */
	for(;comp<n_comp1;comp++) {
		if(locus<n_markers) allele_trans=mark->allele_trans[comp];
		else allele_trans=all_trans;
		if(locus<n_markers && !(markers[locus].element->type&ST_MODEL) && syst_var[RECODE_OPTION]) {
			/* Find out which alleles are used in this component */
			for(i=0;i<n_all;i++) all_flag[i]=0;
			for(j=0;j<comp_size[comp];j++) {
				i=perm[j+id];
				if(!(id_array[i].flag&HAS_GDATA)) continue;
				if(id_array[i].haplo[0]) {
					k=id_array[i].haplo[0][locus];
					if(k) all_flag[k-1]=1;
					k=id_array[i].haplo[1][locus];
					if(k) all_flag[k-1]=1;
				}
			}
			for(i=n_all1=0;i<n_all;i++) n_all1+=all_flag[i];
			n_all1++;
			if(n_all1>n_all) n_all1=n_all;
		} else n_all1=n_all;
		if(n_all1<n_all) {
			for(j=i=0;i<n_all;i++)
			  if(all_flag[i]) allele_trans[j++]=i;
			for(;j<n_all;j++) allele_trans[j]= -1;
		} else for(i=0;i<n_all;i++) allele_trans[i]=i;
		for(i=0;i<n_all;i++) all_flag[i]=n_all1-1;
		if(locus<n_markers && n_all>markers[locus].element->n_levels) allele_trans[n_all-1]= -1;
		/* While all_flag will (temporarily) have the translation table from the
		 * old codes to the new codes */
		for(i=0;i<n_all;i++) if((j=allele_trans[i])>=0) all_flag[j]=i;
		if((size_t)n_all1>LK_LONG_BIT) abt(__FILE__,__LINE__,"%s(): No. segregating alleles for marker %s exceeds %d\n",__func__,markers[locus].var->name,LK_LONG_BIT);
		/* Use all_flag to recode haplotypes */
		if(n_all1<n_all) {
			for(j=0;j<comp_size[comp];j++) {
				i=perm[j+id];
				if(!(id_array[i].flag&HAS_GDATA)) continue;
				if(id_array[i].haplo[0]) {
					k=id_array[i].haplo[0][locus];
					if(k) id_array[i].haplo[0][locus]=all_flag[k-1]+1;
					k=id_array[i].haplo[1][locus];
					if(k) id_array[i].haplo[1][locus]=all_flag[k-1]+1;
				}
			}
			rec_gen_flag=1;
		}
		for(k=j=0;k<comp_size[comp];k++)	{
			i=perm[id+k];
			if(!(id_array[i].flag&IS_PRUNED)) j++;
		}
		if(!j || n_all1<2 || !ge_option) {
			id+=comp_size[comp];
			continue;
		}
		/* Genotype elimination */
		if(!(all_set=malloc(sizeof(void *)*n_all1))) ABT_FUNC(MMsg);
		if(!(all_set[0]=calloc((size_t)(comp_size[comp]*(n_all1+3)),sizeof(lk_long)))) ABT_FUNC(MMsg);
		for(i=1;i<n_all1;i++) all_set[i]=all_set[i-1]+comp_size[comp];
		req_set[0]=all_set[i-1]+comp_size[comp];
		req_set[1]=req_set[0]+comp_size[comp];
		req_set[2]=req_set[1]+comp_size[comp];
		mask=(1<<n_all1)-1;
		for(j=0;j<comp_size[comp];j++) {
			i=perm[j+id];
			ids=id_array[i].sire;
			idd=id_array[i].dam;
			if(id_array[i].flag&IS_PRUNED) continue;
			if(linktype && !id_array[i].sex) {
				(void)fputs("Individual ",stderr);
				print_orig_id(stderr,i+1,0);
				(void)fputs(" has unknown sex\n",stderr);
				err=-1;
				continue;
			}
			if(locus<n_markers && (id_array[i].flag&HAS_GDATA)) {
				if(id_array[i].haplo[0]) {
					ch[0]=id_array[i].haplo[0][locus];
					ch[1]=id_array[i].haplo[1][locus];
				} else ch[0]=ch[1]=0;
				if(!ch[0] && ch[1]) {
					id_array[i].haplo[0][locus]=ch[1];
					id_array[i].haplo[1][locus]=0;
					ch[0]=ch[1];
					ch[1]=0;
				}
				if(linktype==LINK_Y) {
					if(id_array[i].sex==2 || (ch[1] && (ch[0]!=ch[1]))) {
						(void)fputs("(Y-linked marker): Individual ",stderr);
						print_orig_id(stderr,i+1,0);
						if(id_array[i].sex==2) (void)fputs(" is female and has genetic data - deleting\n",stderr);
						else (void)fputs(" has 2 different haplotypes - deleting\n",stderr);
						if(!fname1)	{
							if(!(fname1=get_errfile(marker_name,1))) ABT_FUNC(MMsg);
							ferr=fopen(fname1,"w");
						}
						if(ferr)	{
							(void)fprintf(ferr,"%d ",i+1);
							print_orig_id(ferr,i+1,0);
							(void)fputc('\n',ferr);
						}
						bad_cnt++;
						ch[0]=ch[1]=0;
						id_array[i].haplo[0][locus]=id_array[i].haplo[1][locus]=0;
					}
					ch[1]=0;
				} else if(linktype==LINK_X && id_array[i].sex==1) {
					if(ch[1] && ch[0]!=ch[1]) {
						(void)fputs("(X-linked marker): Individual ",stderr);
						print_orig_id(stderr,i+1,0);
						(void)fputs(" is male with 2 different haplotypes - deleting\n",stderr);
						if(!fname1)	{
							if(!(fname1=get_errfile(marker_name,1))) ABT_FUNC(MMsg);
							ferr=fopen(fname1,"w");
						}
						if(ferr) {
							(void)fprintf(ferr,"%d ",i+1);
							print_orig_id(ferr,i+1,0);
							(void)fputc('\n',ferr);
						}
						bad_cnt++;
						ch[0]=ch[1]=0;
						id_array[i].haplo[0][locus]=id_array[i].haplo[1][locus]=0;
						for(k=0;k<n_all1;k++) all_set[k][j]=mask;
					}
					ch[1]=0;
				}
				if(ch[1]) {
					for(k=0;k<n_all1;k++) all_set[k][j]=0;
					all_set[ch[0]-1][j]=1<<(ch[1]-1);
					all_set[ch[1]-1][j]=1<<(ch[0]-1);
				} else if(ch[0]) {
					m1=1<<(ch[0]-1);
					if(linktype && id_array[i].sex==1) {
						for(k1=0;k1<n_all1;k1++) all_set[k1][j]=0;
						if(linktype==LINK_X) all_set[ch[0]-1][j]=1;
						else all_set[0][j]=m1;
					} else {
						for(k1=0;k1<n_all1;k1++) all_set[k1][j]=m1;
						all_set[ch[0]-1][j]=mask;
					}
				} else {
					if(linktype==LINK_AUTO) for(k=0;k<n_all1;k++) all_set[k][j]=mask;
					else if(linktype==LINK_Y) {
						for(k=1;k<n_all1;k++) all_set[k][j]=0;
						if(id_array[i].sex==1) all_set[0][j]=mask;
					} else {
						m1=id_array[i].sex==1?1:mask;
						for(k=0;k<n_all1;k++) all_set[k][j]=m1;
					}
				}
			} else {
				if(linktype==LINK_AUTO) for(k=0;k<n_all1;k++) all_set[k][j]=mask;
				else if(linktype==LINK_Y) {
					for(k=1;k<n_all1;k++) all_set[k][j]=0;
					if(id_array[i].sex==1) all_set[0][j]=mask;
				} else {
					m1=id_array[i].sex==1?1:mask;
					for(k=0;k<n_all1;k++) all_set[k][j]=m1;
				}
			}
		}
		if(err) ABT_FUNC(AbMsg);
		for(n_fam=j=0;j<n_families;j++) if(!famflag[j])	{
			if(id_array[family[j].kids[0]].component==(comp+1)) famlist[n_fam++]=j;
		}
		if(locus<n_markers && n_fam) {
			for(j=0;j<n_fam;j++)	{
				k=famlist[j];
				ids=family[k].sire;
				idd=family[k].dam;
				if(!ids)	{
					kid=family[k].kids[0];
					if(id_array[kid].flag&HAS_GDATA) famflag[k]=1;
				} else if((id_array[ids-1].flag&HAS_GDATA) || (id_array[idd-1].flag&HAS_GDATA)) famflag[k]=1;
				else {
					for(i=0;i<family[k].nkids;i++) {
						kid=family[k].kids[i];
						if(id_array[kid].flag&HAS_GDATA) {
							famflag[k]=1;
							break;
						}
					}
				}
			}
			k1=0;
			do {
				k=0;
				for(j=0;j<n_fam;j++) {
					i=famlist[j];
					if(!famflag[i]) continue;
					err=DoFamily(i,n_all1,locus,linktype);
					k1++;
					k=1;
					if(err) {
						if(!silent_flag) ABT_FUNC("Pedigree errors, aborting\n");
						err=i+1;
						break;
					}
				}
			} while(k && !err);
		}
		if(err) {
			free(all_set[0]);
			free(all_set);
			break;
		}
		if(locus<n_markers && syst_var[RECODE_OPTION]==2 && !(markers[locus].element->type&ST_MODEL)) {
			/* Find required allele set for each individual (used for set recoding */
			for(j=comp_size[comp]-1;j>=0;j--) {
				i=perm[j+id];
				sex=id_array[i].sex;
				req_set[0][j]=req_set[1][j]=0;
				if(id_array[i].flag&IS_PRUNED) continue;
				if(linktype==LINK_Y && sex==2) continue;
				if(id_array[i].flag&HAS_GDATA) {
					if(id_array[i].haplo[0]) {
						ch[0]=id_array[i].haplo[0][locus];
						ch[1]=id_array[i].haplo[1][locus];
					} else ch[0]=ch[1]=0;
					if(linktype==LINK_Y || (linktype==LINK_X && sex==1)) {
						if(ch[0]) {
							req_set[linktype==LINK_Y?X_PAT:X_MAT][j]|=1<<(ch[0]-1);
							continue;
						}
					} else {
						if(ch[0]) {
							for(k=0;k<2;k++) if(ch[k])	{
								m1=1<<(ch[k]-1);
								if(all_set[ch[k]-1][j]) req_set[X_MAT][j]|=m1;
								for(k1=0;k1<n_all1;k1++) if(all_set[k1][j]&m1) req_set[X_PAT][j]|=m1;
							}
						}
						if(ch[1]) continue;
					}
				}
				for(k=0;k<id_array[i].nkids;k++) {
					kid=id_array[i].kids[k];
					if(id_array[kid].flag&IS_PRUNED) continue;
					kid1=ped_recode1[kid]-1-id;
					if(linktype==LINK_Y)	{
						if(id_array[kid].sex==2) continue;
						a=req_set[X_PAT][kid1];
						b=all_set[0][j];
						req_set[X_PAT][j]|=(a&b);
					} else if(linktype==LINK_X) {
						if(sex==1 && id_array[kid].sex==1) continue;
						a=req_set[2-sex][kid1];
						for(k1=0;k1<n_all1;k1++) {
							b=all_set[k1][j];
							if(b)	{
								if(id_array[kid].sex==2 && sex==2) req_set[X_PAT][j]|=(a&b);
								m1=1<<k1;
								if(a&m1) req_set[X_MAT][j]|=m1;
							}
						}
					} else {
						a=req_set[2-sex][kid1];
						for(k1=0;k1<n_all1;k1++) {
							b=all_set[k1][j];
							if(b) {
								req_set[X_PAT][j]|=(a&b);
								m1=1<<k1;
								if(a&m1) req_set[X_MAT][j]|=m1;
							}
						}
					}
				}
			}
			/* Find which individuals we can do set recoding on, lump appropriate alleles together */
			for(j=0;j<comp_size[comp];j++) {
				i=perm[j+id];	
				if(id_array[i].flag&IS_PRUNED) continue;
				sex=id_array[i].sex;
				if(linktype==LINK_Y && sex==2) continue;
				/* Convert from required set to unrequired set by xoring with possible set */
				for(b=0,k1=0;k1<n_all1;k1++) {
					a=all_set[k1][j];
					if(!a) continue;
					if(linktype!=LINK_Y)	{
						m1=1<<k1;
						req_set[X_MAT][j]^=m1;
					}
					b|=a;
				}
				if(linktype!=LINK_X || sex==2) req_set[X_PAT][j]^=b;
				/* Find if there are >1 possible but unrequired maternal alleles which can
				 * be lumped into allele km */
				k1=km=0;
				if(linktype!=LINK_Y)	{
					a=req_set[X_MAT][j];
					while(a)	{
						k1++;
						if(a&1) km=k1;
						a>>=1;
						if(km) break;
					}
					if(!a) {
						km=0;
						req_set[X_MAT][j]=0;
					}
				}
				/* Ditto for paternal alleles */
				k1=kp=0;
				if(linktype!=LINK_X || sex==2) {
					a=req_set[X_PAT][j];
					while(a)	{
						k1++;
						if(a&1) kp=k1;
						a>>=1;
						if(kp) break;
					}
					if(!a) {
						kp=0;
						req_set[X_PAT][j]=0;
					}
				}
				/* Lump together possible sets for unused alleles, zero remainder */
				if(km || kp) {
					if(km) {
						for(b=0,k1=0;k1<n_all1;k1++) {
							a=all_set[k1][j];
							if(!a) continue;
							m1=1<<k1;
							if(req_set[X_MAT][j]&m1) {
								b|=a;
								all_set[k1][j]=0;
							}
						}
						all_set[km-1][j]=b;
					}
					if(kp) {
						m1=1<<(kp-1);
						for(k1=0;k1<n_all1;k1++) {
							a=(req_set[X_PAT][j]&all_set[k1][j]);
							if(a) {
								all_set[k1][j]^=a;
								all_set[k1][j]|=m1;
							}
						}
					}
				}
			}
		} else {
			for(j=comp_size[comp]-1;j>=0;j--) {
				i=perm[j+id];
				req_set[0][j]=req_set[1][j]=0;
			}
		}
		err=logical_peel(n_fam,famlist,n_all1,locus,comp,linktype,silent_flag==1?0:lfptr);
		free_hash_blocks();
		for(k=0;k<n_rfuncs;k++) free(r_func[k].id_list);
		free(r_func); 
		free_peelseq(&peelseq_head);
		free(all_set[0]);
		free(all_set);
		id+=comp_size[comp];
		if(err) break;
	}
	if(locus<n_markers) mark=markers+locus;
	else mark=traitlocus;
	free(nfam);
	free(temp_set);
	free(all_flag);
	if(((silent_flag && err) || silent_flag==1) && locus!=n_markers) {
		free(mark->allele_trans[0]);
		free(mark->allele_trans);
		mark->allele_trans=0;
	}
	if(ferr) (void)fclose(ferr);
	if(fname1) free(fname1);
	return err;
}

int Genotype_Elimination(int check_flag,char *lfile,int error_check)
{
	FILE *flog,*ferr,*lfptr=0;
	char *fname1;
	int i,j,j_1,ids,idd,locus,*bk[2],k,k1,k2,k3,k4,sc,fam1,fam,*tlist,*fam_list,linktype,*blank=0,comp;
	char bf[256],bf1[256],bf2[256],bf3[256];
	struct Link *pl;
	
	errno=0;
	if(!error_check) check_flag=0;
	ge_option=error_check;
	trace_peel=syst_var[PEEL_TRACE];
	if(check_flag) {
		if(!(bk[0]=malloc(sizeof(int)*2*pruned_ped_size))) ABT_FUNC(MMsg);
		bk[1]=bk[0]+pruned_ped_size;
		if(!(blank=malloc(sizeof(int)*pruned_ped_size))) ABT_FUNC(MMsg);
	}
	if(!(perm=malloc(sizeof(int)*(n_families+2*pruned_ped_size)))) ABT_FUNC(MMsg);
	tlist=perm+pruned_ped_size;
	fam_list=tlist+pruned_ped_size;
	for(i=0;i<ped_size;i++) {
		j=ped_recode1[i];
		if(j) perm[j-1]=i;
	}
	if(OutputLaurFile) {
		if(!(lfptr=fopen(OutputLaurFile,"w"))) perror("Couldn't open output file");
	}
	for(locus=0;locus<n_markers+(traitlocus?1:0);locus++) {
		bad_cnt=0;
		if(locus<n_markers) {
			if(!markers[locus].element->n_levels) continue;
			pl=links;
			i=markers[locus].link-1;
			while(i && pl) {
				pl=pl->next;
				i--;
			}
			if(!pl) ABT_FUNC("Invalid linkage group\n");
			linktype=pl->type;
		} else linktype=LINK_AUTO;
		ferr=0;
		fname1=0;
		marker_name=get_marker_name(locus);
		if(locus==n_markers) (void)printf("Processing trait locus '%s'\n",marker_name);
		else (void)printf("Processing marker '%s' (no. alleles = %d)\n",marker_name,markers[locus].element->n_levels);
		if(!check_flag || locus==n_markers) {
			silent_flag=0;
			(void)Check_Locus(locus,0,linktype,lfptr);
		} else if(check_flag) {
			for(j=0;j<pruned_ped_size;j++) {
				i=perm[j];
				if(id_array[i].haplo[0] && (id_array[i].haplo[0][locus] || id_array[i].haplo[1][locus])) {
					blank[j]=0;
					for(k=0;k<2;k++) bk[k][j]=id_array[i].haplo[k][locus];
				} else blank[j]= -1;
			}
			silent_flag=2;
			fam=Check_Locus(locus,0,linktype,lfptr);
			if(fam) {
				if(rec_gen_flag) {
					for(j=0;j<pruned_ped_size;j++) if(blank[j]>=0) {	
						i=perm[j];
						for(k=0;k<2;k++) id_array[i].haplo[k][locus]=bk[k][j];
					}
				}
				markers[locus].element->n_levels=n_all_old;
				if(fam<0) ABT_FUNC(AbMsg);
				(void)fputs("Genotype inconsistency - searching for errors\n",stderr);
				if(!(fname1=get_errfile(marker_name,0))) ABT_FUNC(MMsg);
				if((ferr=fopen(fname1,"r"))) {
					k1=0;
					(void)fprintf(stderr,"Reading 'bad' individuals from %s\n",fname1);
					for(;;) {
						if(family_id) {
							i=fscanf(ferr,"%255s %255s %255s %255s",bf3,bf,bf1,bf2);
							if(i!=4) break;
							j=family_recode[0]->type;
							j_1=find_id_code(bf3,j,-1);
						} else {
							i=fscanf(ferr,"%255s %255s %255s",bf,bf1,bf2);
							if(i!=3) break;
							j_1=0;
						}
						j=ped_recode[0]->type;
						i=find_id_code(bf,j,j_1);
						ids=find_id_code(bf1,j,j_1);
						idd=find_id_code(bf2,j,j_1);
						if(i<1 || id<0 || ids<0) {
							k1=1;
							break;
						}
						j=ped_recode1[i-1]; 
						if(!j || blank[j-1]== -1) {
							k1=1;
							(void)fprintf(stderr,"Individual %s specified with no marker data\n",bf);
							break;
						}
						if(i==ids || i==idd) {
							for(k=0;k<id_array[i-1].nfam;k++) {
								k2=id_array[i-1].famlist[k];
								if(ids==family[k2].sire && idd==family[k2].dam) {
									blank[j-1]=k2+1;
									break;
								}
							}
							if(k==id_array[i-1].nfam) {
								(void)fprintf(stderr,"Can not find family for id %s, parents %s,%s\n",bf,bf1,bf2);
								k1=1;
								break;
							}
						} else {
							blank[j-1]=id_array[i-1].family;
							if(!blank[j-1]) {
								(void)fprintf(stderr,"Marker error file corrupt - individual %s does not match family (%s %s)\n",bf,bf1,bf2);
								k1=1;
								break;
							}
						}
						if(!blank[j-1]) {
							ABT_FUNC("Internal error - non-blank blanked individual...\n");
						}
						if(ids!=family[blank[j-1]-1].sire || idd!=family[blank[j-1]-1].dam) {
							(void)fprintf(stderr,"Marker error file corrupt - individual %s does not match family (%s %s)\n",bf,bf1,bf2);
							k1=1;
							break;
						}
						for(k=0;k<2;k++) id_array[i-1].haplo[k][locus]=0;
					}
					(void)fclose(ferr);
					ferr=0;
					if(!k1) {
						fam1=Check_Locus(locus,0,linktype,lfptr);
						if(fam1) {
							k1=1;
							(void)fprintf(stderr,"Resulting configuration not consistent\n");
							markers[locus].element->n_levels=n_all_old;
						}
					}
				} else k1=1;
				if(k1) {
					(void)fputs("Searching for 'bad' subset\n - Pass 1: ",stdout);
					(void)fflush(stdout);
					for(j=0;j<pruned_ped_size;j++) if(blank[j]>=0) {
						i=perm[j];
						blank[j]=0;
						for(k=0;k<2;k++) id_array[i].haplo[k][locus]=bk[k][j];
					}
					sc=0;
					sig_caught=0;
					catch_sigs=1;
					silent_flag=1;
					k4=0;
					while(fam && !sig_caught) {
						add_to_list(fam,&k4,fam_list);
						k2=0;
						/* Put all (non-pruned) family members into tlist */
						i=family[fam-1].sire;
						if(i) {
							j=ped_recode1[i-1];
							if(j) tlist[k2++]=j-1;
						}
						i=family[fam-1].dam;
						if(i) {
							j=ped_recode1[i-1];
							if(j) tlist[k2++]=j-1;
						}
						for(k1=0;k1<family[fam-1].nkids;k1++) {
							i=family[fam-1].kids[k1];
							j=ped_recode1[i];
							if(j) tlist[k2++]=j-1;
						}
						comp=id_array[i].component;
						/* blank genotypes for everyone in the family */
						for(k1=0;k1<k2;k1++)	{
							j=tlist[k1];
							if(blank[j]>=0) {
								i=perm[j];
								blank[j]=fam;
								for(k=0;k<2;k++) id_array[i].haplo[k][locus]=0;
							}
						}
						fam1=Check_Locus(locus,-comp,linktype,lfptr);
						if(rec_gen_flag) {
							for(j=0;j<pruned_ped_size;j++) if(blank[j]>=0)	{
								i=perm[j];
								for(k=0;k<2;k++) id_array[i].haplo[k][locus]=blank[j]?0:bk[k][j];
							}
						}
						/* Is the same family still giving problems? */
						if(fam1==fam) {
							for(k1=0;k1<k2;k1++)	{
								j=tlist[k1];
								i=perm[j];
								k3=id_array[i].family;
								if(k3!=fam && k3) {
									blank_fam(k3,blank,locus);
									(void)fputc('.',stdout);
									(void)fflush(stdout);
									add_to_list(k3,&k4,fam_list);
								}
								for(k=0;k<id_array[i].nfam;k++) {
									k3=id_array[i].famlist[k]+1;
									if(k3!=fam) {
										blank_fam(k3,blank,locus);
										(void)fputc('.',stdout);
										(void)fflush(stdout);
										add_to_list(k3,&k4,fam_list);
									}
								}
							}
							fam1=Check_Locus(locus,-comp,linktype,lfptr);
							if(rec_gen_flag) {
								for(j=0;j<pruned_ped_size;j++) if(blank[j]>=0) {
									i=perm[j];
									for(k=0;k<2;k++) id_array[i].haplo[k][locus]=blank[j]?0:bk[k][j];
								}
							}
							if(fam1==fam){
								(void)fprintf(stderr,"Sort of bug - I can't cope with this situation\n");
								silent_flag=0;
								(void)Check_Locus(locus,0,linktype,lfptr);
							}
						}
						fam=fam1;
						(void)fputc('.',stdout);
						(void)fflush(stdout);
					}
 					silent_flag=1;
					(void)printf("\n - Pass 2 %4d",k4);
					(void)fflush(stdout);
					while(k4 && !sig_caught) {
						k4--;
						fam1=fam_list[k4];
						k2=0;
						i=family[fam1-1].sire;
						if(i)	{
							j=ped_recode1[i-1];
							if(j && blank[j-1]>=0) tlist[k2++]=j-1;
						}
						i=family[fam1-1].dam;
						if(i) {
							j=ped_recode1[i-1];
							if(j && blank[j-1]>=0) tlist[k2++]=j-1;
						}
						for(k1=0;k1<family[fam1-1].nkids;k1++) {
							i=family[fam1-1].kids[k1];
							j=ped_recode1[i];
							if(j && blank[j-1]>=0) tlist[k2++]=j-1;
						}
						comp=id_array[i].component;
						for(k1=0;k1<k2;k1++)	{
							j=tlist[k1];
							i=perm[j];
							blank[j]=0;
							for(k=0;k<2;k++) id_array[i].haplo[k][locus]=bk[k][j];
						}
						fam=Check_Locus(locus,comp,linktype,lfptr);
						if(rec_gen_flag) {
							for(j=0;j<pruned_ped_size;j++) if(blank[j]>=0) {
								i=perm[j];
								for(k=0;k<2;k++) id_array[i].haplo[k][locus]=blank[j]?0:bk[k][j];
							}
						}
						if(fam) {
							for(;k1>0;k1--) {
								j=tlist[k1-1];
								i=perm[j];
								blank[j]=fam1;
								for(k=0;k<2;k++) id_array[i].haplo[k][locus]=0;
								fam=Check_Locus(locus,comp,linktype,lfptr);
								if(fam) blank[j]=0;
								if(rec_gen_flag) {
									for(j=0;j<pruned_ped_size;j++) if(blank[j]>=0) {
										i=perm[j];
										for(k=0;k<2;k++) id_array[i].haplo[k][locus]=blank[j]?0:bk[k][j];
									}
								} else if(fam) for(k=0;k<2;k++) id_array[i].haplo[k][locus]=bk[k][j];
								markers[locus].element->n_levels=n_all_old;
								if(!fam) break;
							}
							if(!k1) {
								for(k1=0;k1<k2;k1++)	{
									j=tlist[k1];
									i=perm[j];
									blank[j]=fam1;
									for(k=0;k<2;k++) id_array[i].haplo[k][locus]=0;
								}
								for(k1=0;k1<k2;k1++)	{
									j=tlist[k1];
									i=perm[j];
									blank[j]=0;
									for(k=0;k<2;k++) id_array[i].haplo[k][locus]=bk[k][j];
									fam=Check_Locus(locus,comp,linktype,lfptr);
									if(fam) blank[j]=fam1;
									if(rec_gen_flag) {
										for(j=0;j<pruned_ped_size;j++) if(blank[j]>=0) {
											i=perm[j];
											for(k=0;k<2;k++) id_array[i].haplo[k][locus]=blank[j]?0:bk[k][j];
										}
									} else if(fam) for(k=0;k<2;k++) id_array[i].haplo[k][locus]=0;
								}
							}
						}
						(void)printf("\b\b\b\b%4d",k4);
						(void)fflush(stdout);
					}
					silent_flag=2;
					if(Check_Locus(locus,0,linktype,lfptr)) {
						ABT_FUNC("Internal error - OOOK!\n");
					}
					(void)fputc('\n',stdout);
					for(sc=j=0;j<pruned_ped_size;j++) if(blank[j]>0) sc++;
					sig_caught=0;
					catch_sigs=0;
					ferr=fopen(fname1,"w");
				}
				if(lfile) flog=fopen(lfile,"a");
				else flog=0;
				if(flog) {
					(void)fputs("\n**************** Removing genotype errors ***************\n\n",flog);
					(void)fprintf(flog,"Processing marker '%s'\nDeleted subset follows:\n\n",marker_name);
				}
				k1=k2=sc=0;
				for(j=0;j<pruned_ped_size;j++) if(blank[j]>=0) {
					k2++;
					i=perm[j];
					if(blank[j]>0) {
						sc++;
						if(flog) print_orig_id(flog,i+1,1);
						if(ferr) {
							if(family_id) {
								print_orig_family(ferr,i+1,0);
								fputc(' ',ferr);
							}
							print_orig_id1(ferr,i+1,1);
							print_orig_id1(ferr,family[blank[j]-1].sire,1);
							print_orig_id1(ferr,family[blank[j]-1].dam,1);
						}
						if(ferr) (void)fputc('\n',ferr);
						if(k1==10) {
							if(flog) (void)fputc('\n',flog);
							k1=0;
						} else k1++;
					}
				}
				sc+=bad_cnt;
				k2+=bad_cnt;
				(void)printf("(%d out of %d = %.3g%%)\n",sc,k2,100.0*(double)sc/(double)k2);
				if(flog) {
					(void)fprintf(flog," (%d out of %d = %.3g%%)\n",sc,k2,100.0*(double)sc/(double)k2);
					(void)fclose(flog);
				}
				if(ferr) (void)fclose(ferr);
			}
		}
		if(fname1) free(fname1);
		free(marker_name);
	}
	if(lfptr) (void)fclose(lfptr);
	free(perm);
	if(check_flag) {
		free(bk[0]);
		free(blank);
	}
	DoFamily(-1,0,0,0);
	min_deg(0,0,0,0,0);
	return locus<(n_markers+(traitlocus?1:0))?1:0;
}
