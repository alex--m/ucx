/**
 * See file LICENSE for terms.
 */

/* extension to use in testing */
int ucm_dlmallopt_get(int param_number) {
  switch(param_number) {
/*
  case M_TRIM_THRESHOLD:
  case M_GRANULARITY:
  case M_MMAP_THRESHOLD:
*/
  default:
    return 0;
  }
}
