/* GP-SPEC.C - Program to specify a Gaussian process model. */

/* Copyright (c) 1996 by Radford M. Neal 
 *
 * Permission is granted for anyone to copy, use, or modify this program 
 * for purposes of research or education, provided this copyright notice 
 * is retained, and note is made of any changes that have been made. 
 *
 * This program is distributed without any warranty, express or implied.
 * As this program was written for research purposes only, it has not been
 * tested to the degree that would be advisable in any important application.
 * All use of this program is entirely at the user's own risk.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "misc.h"
#include "log.h"
#include "prior.h"
#include "model.h"
#include "data.h"
#include "gp.h"


static void usage(void);


/* MAIN PROGRAM. */

void main
( int argc,
  char **argv
)
{
  static gp_spec spec, *gp = &spec;  /* Static so that unused fields are set */
                                     /*   to zero (for future compatibility) */

  log_file logf;
  log_gobbled logg;

  char ps[100];
  char **ap;
  int l;

  /* Look for log file name. */

  if (argc<2) usage();

  logf.file_name = argv[1];

  /* See if we are to display existing specifications. */

  if (argc==2)
  {
    /* Open log file and gobble up initial records. */
  
    log_file_open(&logf,0);

    log_gobble_init(&logg,0);
    gp_record_sizes(&logg);

    if (!logf.at_end && logf.header.index==-1)
    { log_gobble(&logf,&logg);
    }
  
    /* Display specifications. */
  
    printf("\n");
  
    if ((gp = logg.data['P'])==0)
    { printf ("No specification of GP priors found\n\n");
      exit(0);
    }

    printf ("Number of inputs:    %d\n", gp->N_inputs);  
    printf ("Number of outputs:   %d\n", gp->N_outputs);
  
    if (gp->has_constant)
    { printf("\nConstant part of covariance: %s\n",prior_show(ps,gp->constant));
    }
    
    if (gp->has_linear)
    { printf("\nLinear part of covariance:   %s\n",prior_show(ps,gp->linear));
    }
  
    if (gp->has_jitter)
    { printf("\nJitter part of covariance:   %s\n",prior_show(ps,gp->jitter));
    }

    if (gp->N_exp_parts>0)
    { printf("\nExponential parts of covariance:\n\n");
      printf("   Scale           Relevance            Power   Flags\n");
      for (l = 0; l<gp->N_exp_parts; l++)
      { char s[10000];
        printf("\n  %-15s", prior_show(ps,gp->exp[l].scale));
        printf(" %-20s", prior_show(ps,gp->exp[l].relevance));
        printf(" %6.3f ",gp->exp[l].power);
        if (list_flags(gp->exp[l].flags,gp->N_inputs,Flag_delta,s))
        { printf("  delta%s",s);
        }
        if (list_flags(gp->exp[l].flags,gp->N_inputs,Flag_omit,s))
        { printf("  omit%s",s);
        }
        printf("\n");
      }
    }

    printf("\n");
  
    log_file_close(&logf);
  
    exit(0);
  }

  /* Otherwise, figure out form and priors from program arguments. */

  gp->has_constant = 0;
  gp->has_linear = 0; 
  gp->has_jitter = 0;
  gp->N_exp_parts = 0;

  ap = argv+2;

  if (*ap==0 || (gp->N_inputs = atoi(*ap++))<=0) usage();
  if (*ap==0 || (gp->N_outputs = atoi(*ap++))<=0) usage();

  if (*ap!=0 && strcmp(*ap,"/")!=0) 
  { if (strcmp(*ap,"-")!=0)
    { gp->has_constant = 1;
      if (!prior_parse(&gp->constant,*ap)) usage();
    }
    ap += 1;
  }

  if (*ap!=0 && strcmp(*ap,"/")!=0) 
  { if (strcmp(*ap,"-")!=0)
    { gp->has_linear = 1;
      if (!prior_parse(&gp->linear,*ap)) usage();
    }
    ap += 1;
  }

  if (*ap!=0 && strcmp(*ap,"/")!=0) 
  { if (strcmp(*ap,"-")!=0)
    { gp->has_jitter = 1;
      if (!prior_parse(&gp->jitter,*ap)) usage();
    }
    ap += 1;
  }

  l = 0;

  while (*ap!=0)
  {
    if (l==Max_exp_parts) 
    { fprintf(stderr,"Too many exponential parts in covariance (max %d)\n",
              Max_exp_parts);
      exit(1);
    }

    ap += 1;

    if (*ap==0 || !prior_parse(&gp->exp[l].scale,*ap++)) usage();
    if (*ap==0 || !prior_parse(&gp->exp[l].relevance,*ap++)) usage();

    gp->exp[l].power = 2;

    if (*ap!=0 && strcmp(*ap,"/")!=0 && strchr("0123456789.+-",**ap))
    { if ((gp->exp[l].power = atof(*ap++)) <= 0 || gp->exp[l].power>2) usage();
    }

    while (*ap!=0 && strcmp(*ap,"/")!=0)
    {
      if (*ap!=0 && strncmp(*ap,"delta",5)==0)
      { parse_flags (*ap+5, gp->exp[l].flags, gp->N_inputs, Flag_delta);
      }
      else if (*ap!=0 && strncmp(*ap,"omit",4)==0)
      { parse_flags (*ap+4, gp->exp[l].flags, gp->N_inputs, Flag_omit);
      }
      else
      { fprintf(stderr,"Unknown flag argument: %s\n",*ap);
        exit(1);
      }

      ap += 1;
    }

    l += 1;
  }

  gp->N_exp_parts = l;

  /* Check for illegal prior specifications. */

  if (gp->has_constant && (gp->constant.scale || gp->constant.alpha[1]!=0 
                                              || gp->constant.alpha[2]!=0))
  { fprintf(stderr,"Illegal prior for constant part of covariance\n");
    exit(1); 
  }

  if (gp->has_linear && gp->linear.alpha[2]!=0)
  { fprintf(stderr,"Illegal prior for linear part of covariance\n");
    exit(1); 
  }

  for (l = 0; l<gp->N_exp_parts; l++)
  { if (gp->exp[l].scale.scale || gp->exp[l].scale.alpha[1]!=0
                               || gp->exp[l].scale.alpha[2]!=0)
    { fprintf(stderr,"Illegal prior for scale in exp part of covariance\n");
      exit(1);
    }
    if (gp->exp[l].relevance.alpha[2]!=0)
    { fprintf(stderr,"Illegal prior for relevance in exp part of covariance\n");
      exit(1);
    }
  }

  /* Create log file and write records. */

  log_file_create(&logf);

  logf.header.type = 'P';
  logf.header.index = -1;
  logf.header.size = sizeof *gp;
  log_file_append(&logf,gp);

  log_file_close(&logf);

  exit(0);
}


/* DISPLAY USAGE MESSAGE AND EXIT. */

static void usage(void)
{
  fprintf(stderr,
   "Usage: gp-spec log-file N-inputs N-outputs\n");
  fprintf(stderr,
   "               [ const-part [ linear-part [ jitter-part ] ] ]\n");
  fprintf(stderr,
   "               { / scale-prior relevance-prior [ power ] }\n");
  fprintf(stderr,
   "   or: gp-spec log-file (to display stored specifications)\n");
  fprintf(stderr,
   "Prior: [x]Width[:[Alpha-high][:[Alpha-low]]]\n");

  exit(1);
}

