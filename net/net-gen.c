/* NET-GEN.C - Program to generate networks (eg, from prior distribution). */

/* Copyright (c) 1995 by Radford M. Neal 
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
#include "net.h"
#include "log.h"
#include "rand.h"


/* MAIN PROGRAM. */

void main
( int argc,
  char **argv
)
{
  net_arch *a;
  net_priors *p;

  net_sigmas  sigmas, *s = &sigmas;
  net_params  params, *w = &params;

  int max_index, index, fix;
  double value;

  log_file logf;
  log_gobbled logg;
 
  int i;

  /* Look at program arguments. */

  max_index = 0;
  value = 0;

  fix = argc>2 && strcmp(argv[2],"fix")==0 ? 2
      : argc>3 && strcmp(argv[3],"fix")==0 ? 3
      : argc;

  if (argc<2 || argc>fix+2
   || fix>2 && (max_index = atoi(argv[2]))<=0 && strcmp(argv[2],"0")!=0
   || fix+1<argc && (value = atof(argv[fix+1]))<=0)
  { fprintf(stderr,
      "Usage: net-gen log-file [ max-index ] [ \"fix\" [ value ] ]\n");
    exit(1);
  }

  logf.file_name = argv[1];

  /* Open log file and read network architecture and priors. */

  log_file_open (&logf, 1);

  log_gobble_init(&logg,0);
  logg.req_size['A'] = sizeof *a;
  logg.req_size['P'] = sizeof *p;
  logg.req_size['r'] = sizeof (rand_state);

  while (!logf.at_end && logf.header.index<0)
  { log_gobble(&logf,&logg);
  }
  
  if ((a = logg.data['A'])==0)
  { fprintf(stderr,"No architecture specification in log file\n");
    exit(1);
  }

  if ((p = logg.data['P'])==0)
  { fprintf(stderr,"No prior specification in log file\n");
    exit(1);
  }

  /* Allocate space for parameters and hyperparameters. */

  s->total_sigmas = net_setup_sigma_count(a);
  w->total_params = net_setup_param_count(a);

  s->sigma_block = chk_alloc (s->total_sigmas, sizeof (net_sigma));
  w->param_block = chk_alloc (w->total_params, sizeof (net_param));

  net_setup_sigma_pointers (s, a);
  net_setup_param_pointers (w, a);

  /* Read last records in log file to see where to start, and to get random
     number state left after last network was generated. */

  index = log_gobble_last(&logf,&logg);

  if (logg.last_index<0) 
  { index = 0;
  }

  if (index>max_index)
  { fprintf(stderr,"Networks up to %d already exist in log file\n",max_index);
    exit(1);
  }

  if (logg.data['r']!=0) 
  { rand_use_state(logg.data['r']);
  }

  /* Generate new networks and write them to the log file. */

  for ( ; index<=max_index; index++)
  {
    net_prior_generate (w, s, a, p, argv[fix]!=0, value);

    logf.header.type = 'S';
    logf.header.index = index;
    logf.header.size = s->total_sigmas * sizeof (net_sigma);
    log_file_append (&logf, s->sigma_block);

    logf.header.type = 'W';
    logf.header.index = index;
    logf.header.size = w->total_params * sizeof (net_param);
    log_file_append (&logf, w->param_block);

    if (argv[fix]==0)
    { logf.header.type = 'r';
      logf.header.index = index;
      logf.header.size = sizeof (rand_state);
      log_file_append (&logf, rand_get_state());
    }
  }

  log_file_close(&logf);

  exit(0);
}
