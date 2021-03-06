#ifndef _UTILS_H_
#define _UTILS_H_

#ifdef HAVE_REGCOMP
#include <sys/types.h>
#include <regex.h>
#endif

#include <time.h>

#ifndef WORD_BIT
#define WORD_BIT (sizeof(int)<<3)
#endif

#ifndef LONG_BIT
#define LONG_BIT (sizeof(long)<<3)
#endif

#define   READ	   0
#define   WRITE	1

#define DEFAULT_FILE_PREFIX "loki"

/* Control levels of output to stdout */
#define NO_OUTPUT 3
#define OUTPUT_QUIET 2
#define OUTPUT_NORMAL 0
#define OUTPUT_VERBOSE 1
#define VERBOSE_LEVELS_MASK 3
#define NON_INTERACTIVE 8 /* Set by program */

#define UTL_NULL_POINTER 1
#define UTL_NO_MEM 2
#define UTL_BAD_STAT 3
#define UTL_BAD_DIR 4
#define UTL_BAD_PERM 5
#define UTL_MAX_ERR 5

typedef struct {
  time_t start_time;
  double extra_time,extra_stime,extra_utime;
} loki_time;


#define LK_FILTER 0
#define LK_SEEDFILE 1
#define LK_PHENFILE 2
#define LK_DUMPFILE 3
#define LK_OUTPUTFILE 4
#define LK_FREQFILE 5
#define LK_HAPLOFILE 6
#define LK_POLYFILE 7
#define LK_POSFILE 8
#define LK_IBDFILE 9
#define LK_IBDDIR 10
#define LK_LOGFILE 11
#define LK_GENERRFILE 12

#define LK_NUM_NAMES 13

struct move_stats {
  int success;
  int n;
};

typedef struct {
  char **toks;
  int n_tok;
  int size;
} tokens;

struct lk_param {
  int analysis;
  int map_function;
  int limit_timer_type;
  double limit_time;
  double lm_ratio;
  int bv_iter;
  int n_tloci;
  int max_tloci;
  int min_tloci;
  int dump_freq;
  int sample_from[2];
  int sample_freq[2];
  int start_tloci;
  int output_type;
  int output_haplo;
  int compress_ibd;
  int ranseed_set;
  int est_aff_freq;
  int num_iter;
  int debug_level;
  int prune_option;
  int peel_trace;
  int verbose_level;
  int error_correct_type;
  int si_mode;
  int ibd_mode;
  int genv_out; /* For stat5 EWD */
  int pseudo_flag;
  int pseudo_start;
  int pseudo_freq;
  struct output_gen *Output_Gen;
};

#define NUM_SYSTEM_VAR 14

#define SYST_NO_OVERDOMINANT 0
#define SYST_TAU 1
#define SYST_TAU_MODE 2
#define SYST_CENSOR_MODE 3
#define SYST_DEBUG_LEVEL 4
#define SYST_LM_RATIO 5
#define SYST_PEEL_TRACE 6
#define SYST_BACKUPS 7
#define SYST_SI_MODE 8
#define SYST_IBD_OUTPUT 9
#define SYST_RNG 10
#define SYST_GENV_OUT 11 /* For stat5 EWD */
#define SYST_MSCAN_PROBS 12
#define SYST_CAND_GENE_MODEL 13

#define N_MOVE_STATS 8

struct id_data { 
  union {
    long value;
    double rvalue;
  } data;
  int flag;
};

struct lk_system {
  int catch_sigs;
  int sig_caught;
  struct remember *RemBlock;
  struct remember *FirstRemBlock;
  struct id_data syst_var[NUM_SYSTEM_VAR];
  struct move_stats move_stats[N_MOVE_STATS];
  loki_time lktime;
  int n_cov_columns;
  double res_prior_konst;
  int (*strcmpfunc)(const char *,const char *);
};

struct loki {
  /* Common (more or less) to prep and loki stages */
  char *names[LK_NUM_NAMES];
  struct lk_param params;
  struct lk_system sys;
  struct lk_compress *compress;
  /* Loki stage data (i.e., after cleaning) */
  struct lk_ped *pedigree;
  struct lk_markers *markers;
  struct lk_model *models;
  struct lk_data *data; 
  struct lk_peel *peel;
  struct lk_fam *family;
  /* Prep stage data (i.e., before cleaning/recoding etc */
  struct lk_prep *prep;
};

extern const char *FMsg,*IntErr,*MMsg,*AbMsg;
extern int from_abt;

#define free_tokens(x) {free((x)->toks); free(x);}

void set_strcmpfunc(int (*)(const char *,const char *));
void abt(const char *, const int, const char *, ...) _NR_;
double strtodnum(const char *,double,double,char **);
long strtolnum(const char *,long,long,char **);
char *getcolumn(const tokens *,const int,const char *,char **);
char *getstrcolumn(const tokens *,const int,const char *,char **);
double getdnumcolumn(const tokens *,const int,const char *,char **);
long getlnumcolumn(const tokens *,const int,const char *,char **);
char *make_file_name( const char *);
void print_start_time(const char *,const char *,char *,loki_time *);
void print_end_time(void);
int mystrcmp(const char *, const char *);
void qstrip(char *);
tokens *tokenize(char *,const int,tokens *);
tokens *copy_ntokens(tokens *,int);
void gen_perm(int *,int);
void gnu_qsort(void *const,size_t,size_t,int(*)(const void *,const void *));
int txt_print_double(double,FILE *);
int txt_get_double(char *,char **,double *);
int count_bits(unsigned long);
void *reverse_list(void *);
void free_list(void *,void (*)(void *));
int list_length(void *);
void message(const int, const char *, ...);
int check_output(const int);
int proc_verbose_level(const int,const int);
void init_lib_utils(void);
int set_file_prefix(const char *);
int set_file_dir(const char *);
char *utl_error(int);
char *add_file_dir(const char *);
double addlog(const double,const double);
double sublog(const double,const double);
#if !HAVE_ISINF
extern int isinf(double);
#endif
#ifdef HAVE_REGCOMP
tokens *reg_tokenize(char *,regex_t *,tokens *);
#endif

#define ABT_FUNC(msg) abt(__FILE__,__LINE__,"%s(): %s",__func__,msg)
#define copy_tokens(x) copy_ntokens((x),0)
#define get_verbose_level() proc_verbose_level(0,0)
#define set_verbose_level(x) proc_verbose_level(x,1)
#define INFO_MSG 1
#define WARN_MSG 2
#define DEBUG_MSG 4
#define ERROR_MSG 8

#endif
