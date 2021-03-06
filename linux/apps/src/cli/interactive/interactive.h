#ifndef INTERACTIVE_H__
#define INTERACTIVE_H__

#include <libbladeRF.h>
#include "common.h"

#ifdef INTERACTIVE
/**
 * Interactive mode 'main loop'
 *
 * If this function is passed a valid device handle, it will take responsibility
 * for deallocating it before returning.
 *
 * @param   s   CLI state
 *
 * @return  0 on success, non-zero on failure
 */
int interactive(struct cli_state *s);

#else
#define interactive(x) (0)
#define interactive_cleanup(x) do {} while(0)
#endif

#endif
