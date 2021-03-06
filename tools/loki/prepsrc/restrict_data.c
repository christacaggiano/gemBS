/****************************************************************************
 *                                                                          *
 *     Loki - Programs for genetic analysis of complex traits using MCMC    *
 *                                                                          *
 *             Simon Heath - University of Washington                       *
 *                                                                          *
 *                       March 1997                                         *
 *                                                                          *
 * restrict_data.c:                                                         *
 *                                                                          *
 * Handles restricted data sets specified by the USE WHERE statements       *
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
#include <dmalloc.h>
#endif
#include <math.h>
#include <stdio.h>

#include "utils.h"
#include "scan.h"
#include "y.tab.h"

static int op_stack_size=32,op_stack_n;
static struct op_stack *Op_Stack;
static struct remember *temp_mem_block,*tmb;

static int trace_restrict;

static const int promote[]={INTEGER,REAL,LOGICAL,MISSING}; /* Order of promotion */
	
struct op_type
{
	union arg_type arg;
	int type;
};

static void get_op_from_stack(struct operation *ops)
{
	if(!op_stack_n) ABT_FUNC("Internal error - empty stack\n");
	ops->arg=Op_Stack[--op_stack_n].arg;
	ops->type=Op_Stack[op_stack_n].type;
}

static void add_to_op_stack(const struct operation *ops)
{
	if(op_stack_n==op_stack_size) {
		op_stack_size*=2;
		if(!(Op_Stack=realloc(Op_Stack,sizeof(struct op_stack)*op_stack_size))) ABT_FUNC(MMsg);
	}
	Op_Stack[op_stack_n].arg=ops->arg;
	Op_Stack[op_stack_n++].type=ops->type;
}

static struct bin_node *find_hap(struct bin_node *node,struct var_element *elem)
{
	int k;
	struct bin_node *nd=0;
	struct scan_data *sd;
	
	if(node->left)	{
		nd=find_hap(node->left,elem);
		if(nd) return nd;
	}
	sd=node->data;
	for(k=0;k<sd->n_elements;k++) if(sd->element+k==elem) return node;
	if(node->right) nd=find_hap(node->right,elem);
	return nd;
}

static void print_op_arg(const struct operation *ops)
{
	struct bin_node *node;
	struct scan_data *sd;
	int k;
	
	switch(ops->type)	{
	 case VARIABLE:
		node=find_hap(root_var,ops->arg.element);
		if(node) {
			sd=node->data;
			(void)fputs(sd->name,stdout);
			if(sd->vtype&ST_ARRAY) {
				for(k=0;k<sd->n_elements;k++) if(sd->element+k==ops->arg.element) break;
				(void)printf("(%d)",k+1);
			}
		} else (void)fputs("<NULL VAR NAME>",stdout);
		break;
	 case INTEGER:
		(void)printf("%ld",ops->arg.value);
		break;
	 case REAL:
		(void)printf("%f",ops->arg.rvalue);
		break;
	 case LOGICAL:
		if(ops->arg.value) (void)fputs("<TRUE>",stdout);
		else (void)fputs("<FALSE>",stdout);
		break;
	 case MISSING:
		(void)fputs("<MISSING>",stdout);
		break;
	 case STRING:
		if(ops->arg.string) (void)printf("\"%s\"",ops->arg.string);
		else (void)fputs("<NULL STRING>",stdout);
		break;
	 default:
		(void)fputs("<INVALID TYPE>",stdout);
	}
}

static void get_op_type(const struct operation *ops,struct op_type *type,const int id,const int rec)
{
	const struct var_element *elem;
	int i,j,k,marker,flag;
	const struct label_data *node;
	union {
		double rval;
		long val;
	} val;
	
	switch(ops->type) {
	 case LOGICAL:
	 case INTEGER:
	 case MISSING:
		type->type=ops->type;
		type->arg.value=ops->arg.value;
		break;
	 case REAL:
		type->type=REAL;
		type->arg.rvalue=ops->arg.rvalue;
		break;
	 case STRING:
		type->type=STRING;
		type->arg.string=ops->arg.string;
		break;
	 case VARIABLE:
		elem=ops->arg.element;
		if(elem->type&(ST_ID|ST_FAMILY|ST_SIRE|ST_DAM|ST_HAPLO|ST_MARKER)) i=0;
		else if(elem->type&ST_CONSTANT) {
			for(i=0;i<n_id_records;i++) if(id_elements[i]==elem) break;
			if(i==n_id_records) i= -1;
		} else {
			for(i=0;i<n_nonid_records;i++) if(nonid_elements[i]==elem) break;
			if(i==n_nonid_records) i= -1;
		}
		if(i>=0 && (elem->type&(ST_ID|ST_SIRE|ST_DAM|ST_FAMILY|ST_FACTOR|ST_HAPLO|ST_MARKER))) {
			node=0;
			if(elem->type&ST_ID) node=ped_recode[id-1];
			else if(elem->type&(ST_SIRE|ST_DAM|ST_FAMILY|ST_MARKER|ST_HAPLO)) {
				if(elem->type&ST_SIRE) {
					j=id_array[id-1].sire;
					if(j) node=ped_recode[j-1];
				} else if(elem->type&ST_DAM) {
					j=id_array[id-1].dam;
					if(j) node=ped_recode[j-1];
				} else if(elem->type&ST_FAMILY) {
					j=id_array[id-1].fam_code;
					if(j) node=family_recode[j-1];
				} else if(elem->type&ST_HAPLO) {
					if(id_array[id-1].haplo[0]) {
						marker=elem->arg.element->index;
						k=1;
						if(markers[marker].hap_element[0]==elem) k=0;
						else if(markers[marker].hap_element[1]!=elem) ABT_FUNC(IntErr);
						j=id_array[id-1].haplo[k][marker];
						if(j) node=factor_recode[n_factors+marker][j-1];
					}
				} else if(elem->type&ST_MARKER) {
					type->type=LOGICAL;
					type->arg.value=0;
					if(id_array[id-1].haplo[0]) {
						marker=elem->index;
						if(id_array[id-1].haplo[0][marker] || id_array[id-1].haplo[0][marker])
						  type->arg.value=1;
					}
					return;
				} else ABT_FUNC(IntErr);
			} else if(elem->type&ST_CONSTANT) {
				if(id_array[id-1].data) if(id_array[id-1].data[i].flag) {
					j=(int)id_array[id-1].data[i].data.value;
					node=factor_recode[elem->index][j-1];
				}
			} else if(id_array[id-1].data1) {
				if(id_array[id-1].data1[rec][i].flag) {
					j=(int)id_array[id-1].data1[rec][i].data.value;
					node=factor_recode[elem->index][j-1];
				}
			}
			if(!node) {
				type->type=MISSING;
				type->arg.value=0;
			} else if(elem->type&ST_INTTYPE) {
				if(node->type!=INTEGER) ABT_FUNC(IntErr);
				type->type=INTEGER;
				type->arg.value=node->data.value;
			} else {
				if(node->type!=STRING) ABT_FUNC(IntErr);
				type->type=STRING;
				type->arg.string=node->data.string;
			}
			return;
		} else if(i>=0) {
			flag=0;
			if(elem->type&ST_CONSTANT) {
				if(id_array[id-1].data) if(id_array[id-1].data[i].flag) {
					flag=1;
					if(elem->type&ST_INTTYPE) val.val=id_array[id-1].data[i].data.value;
					else val.rval=id_array[id-1].data[i].data.rvalue;
				}
			} else if(id_array[id-1].data1) {
				if(id_array[id-1].data1[rec][i].flag) {
					flag=1;
					if(elem->type&ST_INTTYPE) val.val=id_array[id-1].data1[rec][i].data.value;
					else val.rval=id_array[id-1].data1[rec][i].data.rvalue;
				}
			}
			if(!flag) {
				type->type=MISSING;
				type->arg.value=0;
			} else if(elem->type&ST_INTTYPE) {
				type->type=INTEGER;
				type->arg.value=val.val;
			} else {
				type->type=REAL;
				type->arg.rvalue=val.rval;
			}
			return;
		}
		type->type=MISSING;
		type->arg.value=0;
		break;
	 default:
		abt(__FILE__,__LINE__,"%s(): Internal error: wrong op type (%d)\n",__func__,ops->type);
	}
	return;
}

static void promote_type(struct op_type *type,const int rtype)
{
	switch(rtype) {
	 case MISSING:
	 case LOGICAL:
		if(type->type==INTEGER || type->type==REAL) {
			type->arg.value=1;
			type->type=LOGICAL;
		}
		break;
	 case REAL:
		if(type->type==INTEGER) type->arg.rvalue=(double)type->arg.value;
		else ABT_FUNC(IntErr);
		type->type=REAL;
		break;
	 default:
		ABT_FUNC("Internal error: invalid type\n");
	}
}

static void do_string_op(const int op,const struct op_type *type,const struct op_type *type1,struct operation *res)
{
	char *s,*s1;
	size_t sz,sz1;
	
	s=s1=0;
	if(type->type==STRING) s=type->arg.string;
	if(type1->type==STRING) s1=type1->arg.string;
	if(!(s || s1)) ABT_FUNC(IntErr);
	res->type=LOGICAL;
	switch(op) {
	 case '+':
		res->type=STRING;
		if(s && s1) {
			sz=strlen(s);
			sz1=strlen(s1);
			if(!(res->arg.string=malloc(sz+sz1+1))) ABT_FUNC(MMsg);
			temp_mem_block=AddRemem(res->arg.string,temp_mem_block);
			(void)strncpy(res->arg.string,s,sz+1);
			(void)strncat(res->arg.string,s1,sz1);
		} else if(s) res->arg.string=s;
		else if(s1) res->arg.string=s1;
		break;
	 case '<':
		if(s && s1) res->arg.value=(strcmp(s,s1)<0)?1:0;
		else if(s) res->arg.value=type1->arg.value;
		else res->arg.value=1-type->arg.value;
		break;
	 case '>':
		if(s && s1) res->arg.value=(strcmp(s,s1)>0)?1:0;
		else if(s) res->arg.value=1-type1->arg.value;
		else res->arg.value=type->arg.value;
		break;
	 case '=':
		if(s && s1) res->arg.value=strcmp(s,s1)?0:1;
		else if(s) res->arg.value=type1->arg.value;
		else res->arg.value=type->arg.value;
		break;
	 case NEQSYMBOL:
		if(s && s1) res->arg.value=strcmp(s,s1)?1:0;
		else if(s) res->arg.value=1-type1->arg.value;
		else res->arg.value=1-type->arg.value;
		break;
	 case GEQSYMBOL:
		if(s && s1) res->arg.value=(strcmp(s,s1)<=0)?0:1;
		else if(s) res->arg.value=1-type1->arg.value;
		else res->arg.value=type->arg.value;
		break;
	 case LEQSYMBOL:
		if(s && s1) res->arg.value=(strcmp(s,s1)<=0)?0:1;
		else if(s) res->arg.value=type1->arg.value;
		else res->arg.value=1-type->arg.value;;
		break;
	 case ANDSYMBOL:
		if(s && s1) res->arg.value=1;
		else if(s) res->arg.value=type1->arg.value;
		else res->arg.value=type->arg.value;
		break;
	 case ORSYMBOL:
		res->arg.value=1;
		break;
	 default:
		ABT_FUNC("Internal error: invalid string operator\n");
	}
}

static int try_numeric(struct op_type *type)
{
	long l;
	double x;
	char *s;
	int i=0;
	
	l=strtol(type->arg.string,&s,10);
	if(!(*s)) {
		type->type=INTEGER;
		type->arg.value=l;
		i=1;
	} else {
		x=strtod(type->arg.string,&s);
		if(!(*s)) {
			type->type=REAL;
			type->arg.rvalue=x;
			i=1;
		}
	}
	return i;
}

static void do_arith_op(const int op,const struct operation *ops,const struct operation *ops1,struct operation *res,const int id, const int rec)
{
	struct op_type type,type1;
	int i,t,t1;
	
	get_op_type(ops,&type,id,rec);
	if(ops1) get_op_type(ops1,&type1,id,rec);
	else type1.type=0;
	if(type.type==STRING) {
		if(type1.type==STRING) {
			do_string_op(op,&type,&type1,res);
			return;
		} else if(type1.type==LOGICAL) {
			do_string_op(op,&type,&type1,res);
			return;
		} else if(op==ANDSYMBOL || op==ORSYMBOL) {
			res->type=LOGICAL;
			if(op==ANDSYMBOL && type1.type==MISSING) res->arg.value=0;
			else res->arg.value=1;
			return;
		} else if(type1.type==MISSING && (op=='=' || op==NEQSYMBOL)) {
			res->type=MISSING;
			res->arg.value=0;
			return;
		} else if(!try_numeric(&type)) ABT_FUNC("Error: incompatible operators\n");
	} else if(type1.type==STRING) {
		if(type.type==LOGICAL) {
			do_string_op(op,&type,&type1,res);
			return;
		} else if(op==ANDSYMBOL || op==ORSYMBOL) {
			res->type=LOGICAL;
			if(op==ANDSYMBOL && type1.type==MISSING) res->arg.value=0;
			else res->arg.value=1;
			return;
		} else if(type.type==MISSING && (op=='=' || op==NEQSYMBOL))	{
			res->type=MISSING;
			res->arg.value=0;
			return;
		} else if(!try_numeric(&type1)) ABT_FUNC("Error: incompatible operators\n");
	}
	if(ops1)	{
		t=t1= -1;
		for(i=0;i<4;i++) {
			if(promote[i]==type.type) t=i;
			if(promote[i]==type1.type) t1=i;
		}
		if(t<0 || t1<0) ABT_FUNC(IntErr);
		res->type=(t>t1)?promote[t]:promote[t1];
		if(res->type!=type.type) promote_type(&type,res->type);
		if(res->type!=type1.type) promote_type(&type1,res->type);
	} else res->type=type.type;
	switch(op) {
	 case '+':
		if(res->type==REAL) res->arg.rvalue=type.arg.rvalue+type1.arg.rvalue;
		else res->arg.value=type.arg.value+type1.arg.value;
		break;
	 case '-':
	 case UMINUS:
		if(res->type==REAL) {
			if(ops1) res->arg.rvalue=type.arg.rvalue-type1.arg.rvalue;
			else res->arg.rvalue= -type.arg.rvalue;
		} else {
			if(ops1) res->arg.value=type.arg.value-type1.arg.value;
			else res->arg.value= -type.arg.value;
		}
		break;
	 case '*':
		if(res->type==REAL) res->arg.rvalue=type.arg.rvalue*type1.arg.rvalue;
		else res->arg.value=type.arg.value*type1.arg.value;
		break;
	 case '/':
		i=0;
		if(res->type==REAL) {
			if(type1.arg.rvalue==0.0) i=1;
			else res->arg.rvalue=type.arg.rvalue/type1.arg.rvalue;
		} else {
			if(type1.arg.value==0) i=1;
			else res->arg.value=type.arg.value/type1.arg.value;
		}
		if(i) {
			res->type=LOGICAL;
			res->arg.value=0;
		}
		break;
	 case '<':
		if(res->type==REAL) res->arg.value=(type.arg.rvalue<type1.arg.rvalue);
		else res->arg.value=(type.arg.value<type1.arg.value);
		if(res->type!=MISSING) res->type=LOGICAL;
		break;
	 case '>':
		if(res->type==REAL) res->arg.value=(type.arg.rvalue>type1.arg.rvalue);
		else res->arg.value=(type.arg.value>type1.arg.value);
		if(res->type!=MISSING) res->type=LOGICAL;
		break;
	 case '=':
		if(res->type==REAL) res->arg.value=(type.arg.rvalue==type1.arg.rvalue);
		else res->arg.value=(type.arg.value==type1.arg.value);
		if(res->type!=MISSING) res->type=LOGICAL;
		break;
	 case NEQSYMBOL:
		if(res->type==REAL) res->arg.value=(type.arg.rvalue!=type1.arg.rvalue);
		else res->arg.value=(type.arg.value!=type1.arg.value);
		if(res->type!=MISSING) res->type=LOGICAL;
		break;
	 case GEQSYMBOL:
		if(res->type==REAL) res->arg.value=(type.arg.rvalue>=type1.arg.rvalue);
		else res->arg.value=(type.arg.value>=type1.arg.value);
		if(res->type!=MISSING) res->type=LOGICAL;
		break;
	 case LEQSYMBOL:
		if(res->type==REAL) res->arg.value=(type.arg.rvalue<=type1.arg.rvalue);
		else res->arg.value=(type.arg.value<=type1.arg.value);
		if(res->type!=MISSING) res->type=LOGICAL;
		break;
	 case ANDSYMBOL:
		if(res->type==LOGICAL || res->type==MISSING) res->arg.value=(type.arg.value && type1.arg.value);
		else  {
			res->arg.value=1;
			res->type=LOGICAL;
		}
		break;
	 case ORSYMBOL:
		if(res->type==LOGICAL) res->arg.value=(type.arg.value || type1.arg.value);
		else if(res->type==MISSING) {
			if(!(type.type==MISSING && type1.type==MISSING)) {
				res->arg.value=type.arg.value || type1.arg.value;
				res->type=LOGICAL;
			} else res->arg.value=0;
		} else {
			res->arg.value=1;
			res->type=LOGICAL;
		}
		break;
	 default:
		ABT_FUNC("Internal error: invalid operator\n");
	}
}

static char *op_name(const int op)
{
	switch(op) {
	 case '+': return "+";
	 case '-': return "-";
	 case '*': return "*";
	 case '/': return "/";
	 case '<': return "<";
	 case '>': return ">";
	 case '=': return "=";
	 case NEQSYMBOL: return "!=";
	 case GEQSYMBOL: return ">=";
	 case LEQSYMBOL: return "<=";
	 case ANDSYMBOL: return "AND";
	 case ORSYMBOL: return "OR";
	 case NOTSYMBOL: return "NOT";
	 case UMINUS: return "-";
	 default:
		ABT_FUNC("Internal error: invalid operator\n");
	}
	/* Never gets here */
	return "!FAULT!";
}

static void do_op(const struct operation *ops,const int id,const int rec)
{
	struct operation result,arg1,arg2;
	int op;
	struct op_type type;
		
	op=ops->op;
	if(!ops->type) get_op_from_stack(&arg1);
	else {
		arg1.type=ops->type;
		arg1.arg=ops->arg;
	}
	if(trace_restrict>1) (void)fputs("   ",stdout);
	switch(op) {
	 case 0:
		if(trace_restrict>1) print_op_arg(&arg1);
		get_op_type(&arg1,&type,id,rec);
		result.type=type.type;
		result.arg=type.arg;
		break;
	 case NOTSYMBOL:
		if(trace_restrict>1) {
			(void)printf(" %s ",op_name(op));
			print_op_arg(&arg1);
		}
		result.type=LOGICAL;
		if(arg1.type==LOGICAL || arg1.type==MISSING) result.arg.value=1-arg1.arg.value;
		else result.arg.value=0; 
		break;
	 case UMINUS:
		if(trace_restrict>1) {
			(void)printf(" %s ",op_name(op));
			print_op_arg(&arg1);
		}
		if(arg1.type==LOGICAL) {
			result.arg.value=1-arg1.arg.value;
			result.type=LOGICAL;
		} else do_arith_op(op,&arg1,0,&result,id,rec);
		break;
	 default:
		get_op_from_stack(&arg2);
		if(trace_restrict>1)	{
			print_op_arg(&arg2);
			(void)printf(" %s ",op_name(op));
			print_op_arg(&arg1);
		}
		do_arith_op(op,&arg2,&arg1,&result,id,rec);
	}
	if(trace_restrict>1) {
		(void)printf("  [ = ");
		print_op_arg(&result);
		(void)printf(" ]\n");
	}
	add_to_op_stack(&result);
}

void restrict_data(void)
{
 	int i,j,k,k2,id,marker,rec,res_no;
	struct var_element *elem;
	struct Restrict *restriction;
	struct operation *op,result;
	struct bin_node *node;
	struct scan_data *sd;
	
	(void)printf("Handling data restrictions\n");
	restriction=Restrictions;
	j=0;
	while(restriction) {
		(void)printf("Restriction %d: ",++j);
		for(i=k=0;i<restriction->nvar;i++) {
			elem=restriction->element[i];
			if(!(elem->type&ST_REQUIRED)) continue;
			if(k++) (void)fputc(',',stdout);
			node=find_hap(root_var,elem);
			if(node) {
				sd=node->data;
				(void)fputs(sd->name,stdout);
				if(sd->vtype&ST_ARRAY) {
					for(k2=0;k2<sd->n_elements;k2++) if(sd->element+k2==elem) break;
					(void)printf("(%d)",k2+1);
				}
			} else (void)fputs("<NULL VAR NAME>",stdout);
		}
		if(!restriction->nvar) (void)fputs("<ALL>",stdout);
		(void)fputc('\n',stdout);
		restriction=restriction->next;
	}
	trace_restrict=syst_var[TRACE_RESTRICT];
	if(!(Op_Stack=malloc(sizeof(struct op_stack)*op_stack_size))) ABT_FUNC(MMsg);
	for(id=1;id<=ped_size;id++) if(id_array[id-1].data || id_array[id-1].data1 || id_array[id-1].haplo[0]) {
		if(!(tmb=malloc(sizeof(struct remember)))) ABT_FUNC(MMsg);
		temp_mem_block=tmb;
		temp_mem_block->pos=0;
		temp_mem_block->next=0;
		if(id_array[id-1].flag&4) continue;
		j=id_array[id-1].nrec;
		if(j) k2=j;
		else k2=1;
		restriction=Restrictions;
		res_no=0;
		while(restriction) {
			res_no++;
			for(i=0;i<restriction->nvar;i++) {
				elem=restriction->element[i];
				elem->type&=~ST_FLAG;
			}
			for(rec=0;rec<k2;rec++) {
				op=restriction->Op_List;
				while(op) {
					do_op(op,id,rec);
					op=op->next;
				}
				if(trace_restrict) {
					(void)fputs("ID ",stdout);
					print_orig_id(stdout,id,0);
					(void)printf("  Rest. %d, Rec %d ",res_no,rec+1);
					(void)fputs("  RESULT: ",stdout);
				}
				get_op_from_stack(&result);
				if(op_stack_n) ABT_FUNC("Internal error: some operations remaining on stack\n");
				if(result.type!=LOGICAL) {
					result.arg.value=(result.type==MISSING)?0:1;
					result.type=LOGICAL;
				}
				if(trace_restrict) {
					print_op_arg(&result);
					(void)fputs("\n",stdout);
				}
				if(result.arg.value) for(i=0;i<restriction->nvar;i++)	{
					elem=restriction->element[i];
					if(elem->type&ST_CONSTANT) elem->type|=ST_FLAG;
				} else if(restriction->nvar) {
					for(i=0;i<restriction->nvar;i++) {
						elem=restriction->element[i];
						if(!(elem->type&ST_REQUIRED) || (elem->type&ST_FLAG)) continue;
						if((elem->type&ST_CONSTANT) && rec<(k2-1)) continue;
						if(elem->type&ST_ID)	{
							id_array[id-1].flag|=4;
							if(id_array[id-1].haplo[0]) {
								free(id_array[id-1].haplo[0]);
								id_array[id-1].haplo[0]=0;
								id_array[id-1].haplo[1]=0;
							}
						} else if(elem->type&ST_HAPLO) {
							if(id_array[id-1].haplo[0]) {
								marker=elem->arg.element->index;
								if(markers[marker].hap_element[0]==elem) k=0;
								else k=1;
								id_array[id-1].haplo[k][marker]=0;
							}
						} else if(elem->type&ST_MARKER) {
							if(id_array[id-1].haplo[0]) {
								marker=elem->index;
								id_array[id-1].haplo[0][marker]=id_array[id-1].haplo[1][marker]=0;
							}
						} else if(elem->type&ST_CONSTANT) {
							if(id_array[id-1].data)	for(k=0;k<n_id_records;k++) if(elem==id_elements[k]) {
								id_array[id-1].data[k].flag=0;
								break;
							}
						} else {
							if(id_array[id-1].data1) for(k=0;k<n_nonid_records;k++) if(elem==nonid_elements[k]) {
								id_array[id-1].data1[rec][k].flag=0;
								break;
							}
						}
					}
				} else {
					if(id_array[id-1].haplo[0]) {
						free(id_array[id-1].haplo[0]);
						id_array[id-1].haplo[0]=0;
						id_array[id-1].haplo[1]=0;
					}
					if(id_array[id-1].data) for(k=0;k<n_id_records;k++) id_array[id-1].data[k].flag=0;
					if(id_array[id-1].data1) for(k=0;k<n_nonid_records;k++) id_array[id-1].data1[rec][k].flag=0;
				}
			}
			restriction=restriction->next;
		}
		FreeRemem(tmb);
	}
	free(Op_Stack);
}

void cleanup_unused(void)
{
	int id,k,k2,k3,k4,rec,*tmp,*tmp1,marker;

	for(k=0;k<n_id_records;k++) if(!(id_elements[k]->type&(ST_TRAIT|ST_MODEL))) {
		for(id=0;id<ped_size;id++) if(id_array[id].data) id_array[id].data[k].flag=0;
	}
	for(k=0;k<n_nonid_records;k++) if(!(nonid_elements[k]->type&(ST_TRAIT|ST_MODEL))) {
		for(id=0;id<ped_size;id++) if(id_array[id].data1) {
			for(rec=0;rec<id_array[id].nrec;rec++) id_array[id].data1[rec][k].flag=0;
		}
	}
	/* Are there any id records in the model? */
	for(k=0;k<n_id_records;k++) if(id_elements[k]->type&(ST_TRAIT|ST_MODEL)) break;
	k3=k<n_id_records?1:0;
	/* Are there any nonid records in the model? */
	for(k=0;k<n_nonid_records;k++) if(nonid_elements[k]->type&(ST_TRAIT|ST_MODEL)) break;
	k4=k<n_nonid_records?1:0;
	for(id=0;id<ped_size;id++) {
		if(id_array[id].data) {
			for(k=0;k<n_id_records;k++) if(id_elements[k]->type&(ST_TRAIT|ST_MODEL)) {
				if(!id_array[id].data[k].flag) break;
			}
			if(k<n_id_records) id_array[id].data=0;
		}
		if(k3 && !id_array[id].data) {
			id_array[id].data1=0;
			id_array[id].nrec=0;
		}
		if(id_array[id].data1) {
			for(k2=rec=0;rec<id_array[id].nrec;rec++) if(id_array[id].data1[rec]) {
				for(k=0;k<n_nonid_records;k++) if(nonid_elements[k]->type&(ST_TRAIT|ST_MODEL)) {
					if(!id_array[id].data1[rec][k].flag) break;
				}
				if(k<n_nonid_records) id_array[id].data1[rec]=0;
				else k2=1;
			}
			if(!k2) {
				id_array[id].data1=0;
				id_array[id].nrec=0;
			}
		}
		if(k4 && !id_array[id].data1) id_array[id].data=0;
	}
	for(marker=0;marker<n_markers;marker++) {
		k=markers[marker].element->n_levels;
		if(!k) continue;
		if(!(tmp=calloc((size_t)(k*2),sizeof(int)))) ABT_FUNC(MMsg);
		tmp1=tmp+k;
		for(id=0;id<ped_size;id++) if(id_array[id].haplo[0]) {
			for(k3=0;k3<2;k3++) if((k2=id_array[id].haplo[k3][marker])) tmp[k2-1]++;
		}
		for(k2=k3=0;k2<k;k2++) if(tmp[k2]) k3++;
		if(k3<k) {
			for(k3=k2=0;k2<k;k2++) {
				if(tmp[k2]) {
					factor_recode[n_factors+marker][k3]=factor_recode[n_factors+marker][k2];
					tmp1[k2]=++k3;
				} else tmp1[k2]= -1;
			}
			markers[marker].element->n_levels=k3;
			for(id=0;id<ped_size;id++) if(id_array[id].haplo[0]) {
				for(k3=0;k3<2;k3++) if((k2=id_array[id].haplo[k3][marker])) id_array[id].haplo[k3][marker]=tmp1[k2-1];
			}
		}
		free(tmp);
	}
}

void censored_data(void)
{
 	int j,k,k2,id,rec,cens_no=0;
	struct var_element *elem;
	struct Censor *censor;
	struct operation *op,result;
	struct scan_data *sd;
	
	(void)fputs("Handling censored data codes\n",stdout);
	trace_restrict=syst_var[TRACE_CENSORED];
	if(!(Op_Stack=malloc(sizeof(struct op_stack)*op_stack_size))) ABT_FUNC(MMsg);
	for(id=1;id<=ped_size;id++) if(id_array[id-1].data || id_array[id-1].data1) {
		if(id_array[id-1].flag&4) continue;
		if(!(tmb=malloc(sizeof(struct remember)))) ABT_FUNC(MMsg);
		temp_mem_block=tmb;
		temp_mem_block->pos=0;
		temp_mem_block->next=0;
		j=id_array[id-1].nrec;
		if(j) k2=j;
		else if(id_array[id-1].data) k2=1;
		else k2=0;
		censor=Censored;
		while(censor) {
			cens_no++;
			elem=censor->element;
			for(rec=0;rec<k2;rec++) {
				op=censor->Op_List;
				while(op) {
					do_op(op,id,rec);
					op=op->next;
				}
				if(trace_restrict) {
					(void)fputs("ID ",stdout);
					print_orig_id(stdout,id,0);
					(void)printf("  Cens. %d, Rec %d ",cens_no,rec+1);
					(void)fputs("  RESULT: ",stdout);
				}
				get_op_from_stack(&result);
				if(op_stack_n) ABT_FUNC("Internal error: some operations remaining on stack\n");
				if(result.type!=LOGICAL && result.type!=MISSING) {
					result.type=LOGICAL;
					result.arg.value=1;
				}
				if(trace_restrict) {
					print_op_arg(&result);
					(void)fputs("\n",stdout);
				}
				if(result.arg.value) k=2;
				else k=4;
				if(id_array[id-1].data)	{
					for(j=0;j<n_id_records;j++) if(id_elements[j]==elem) {
						if(id_array[id-1].data[j].flag) {
							if(result.type!=MISSING) id_array[id-1].data[j].flag|=k;
							else {
								id_array[id-1].data[j].flag=0;
								if(scan_warn_n++<max_scan_warnings) {
									print_orig_id(stderr,id,0);
									sd=elem->arg.var->data;
									(void)fprintf(stderr," - No censoring information for %s. Record will be ignored.\n",sd->name);
								}
							}
						}
						break;
					}
				}
				if(id_array[id-1].nrec && id_array[id-1].data1) {
					for(j=0;j<n_nonid_records;j++) if(nonid_elements[j]==elem) {
						if(id_array[id-1].data1[rec][j].flag) {
							if(result.type!=MISSING) id_array[id-1].data1[rec][j].flag|=k;
							else {
								id_array[id-1].data1[rec][j].flag=0;
								if(scan_warn_n++<max_scan_warnings) {
									print_orig_id(stderr,id,0);
									sd=elem->arg.var->data;
									(void)fprintf(stderr," - No censoring information for %s. Record will be ignored.\n",sd->name);
								}
							}
						}
						break;
					}
				}
			}
			censor=censor->next;
		}
		FreeRemem(tmb);
	}
	free(Op_Stack);
}

static struct operation *proc_op(struct operation *op,int id,int rec) 
{
	static struct operation result;
	
	while(op) {
		do_op(op,id,rec);
		op=op->next;
	}
	if(trace_restrict) {
		(void)fputs("ID ",stdout);
		print_orig_id(stdout,id,0);
		(void)printf("  Aff. - Rec %d ",rec+1);
		(void)fputs("  RESULT: ",stdout);
	}
	get_op_from_stack(&result);
	if(op_stack_n) ABT_FUNC("Internal error: some operations remaining on stack\n");
	if(result.type!=LOGICAL && result.type!=MISSING) {
		result.type=LOGICAL;
		result.arg.value=1;
	}
	if(trace_restrict) {
		print_op_arg(&result);
		(void)fputs("\n",stdout);
	}
	return &result;
}

void affected_data(void)
{
 	int j,k,k2,id,rec;
	struct operation *result;
	
	(void)fputs("Handling affected data codes\n",stdout);
	trace_restrict=syst_var[TRACE_AFFECTED];
	if(!(Op_Stack=malloc(sizeof(struct op_stack)*op_stack_size))) ABT_FUNC(MMsg);
	for(id=1;id<=ped_size;id++) if(id_array[id-1].data || id_array[id-1].data1) {
		if(id_array[id-1].flag&4) continue;
		if(!(tmb=malloc(sizeof(struct remember)))) ABT_FUNC(MMsg);
		temp_mem_block=tmb;
		temp_mem_block->pos=0;
		temp_mem_block->next=0;
		id_array[id-1].affected=0;
		j=id_array[id-1].nrec;
		if(j) k2=j;
		else if(id_array[id-1].data) k2=1;
		else k2=0;
		for(rec=0;rec<k2;rec++)	{
			result=proc_op(Affected,id,rec);
			k=0;
			if(result->type!=MISSING) k=result->arg.value?2:1;
			if(k!=2 && Unaffected) {
				k=0;
				result=proc_op(Unaffected,id,rec);
				if(result->type!=MISSING && result->arg.value) k=1;
				else if(scan_error_n++<max_scan_errors) {
					print_orig_id(stderr,id,0);
					(void)fputs(" - Affected information invalid.\n",stderr);
				}
			}
			if(k) {
				if(id_array[id-1].affected && id_array[id-1].affected!=k) {
					if(scan_error_n++<max_scan_errors) {
						print_orig_id(stderr,id,0);
						(void)fputs(" - Affected information not constant.\n",stderr);
					}
				} else id_array[id-1].affected=k;
			}
		}
		FreeRemem(tmb);
	}
	free(Op_Stack);
}

void proband_data(void)
{
 	int j,k,k2,id,rec;
	struct operation *result;
	
	(void)fputs("Handling proband data codes\n",stdout);
	trace_restrict=syst_var[TRACE_AFFECTED];
	if(!(Op_Stack=malloc(sizeof(struct op_stack)*op_stack_size))) ABT_FUNC(MMsg);
	for(id=1;id<=ped_size;id++) if(id_array[id-1].data || id_array[id-1].data1) {
		id_array[id-1].proband=0;
		if(id_array[id-1].flag&4) continue;
		if(!(tmb=malloc(sizeof(struct remember)))) ABT_FUNC(MMsg);
		temp_mem_block=tmb;
		temp_mem_block->pos=0;
		temp_mem_block->next=0;
		id_array[id-1].affected=0;
		j=id_array[id-1].nrec;
		if(j) k2=j;
		else if(id_array[id-1].data) k2=1;
		else k2=0;
		for(rec=0;rec<k2;rec++)	{
			result=proc_op(Proband,id,rec);
			k=0;
			if(result->type!=MISSING) k=result->arg.value?1:0;
			id_array[id-1].proband=k;
		}
		FreeRemem(tmb);
	}
	free(Op_Stack);
}
