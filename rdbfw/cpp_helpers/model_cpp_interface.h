/* This model interface connects the virtual rdb interface to a C friendly
 * set of methods. If you decide to modify this interface, you will follow
 * these two rules:
 * 1. Thou shalt not try to directly access data contained within the
 *    c++ class which the Model pointer points to.
 * 2. Thou shalt not pass around Model pointers as void *'s, or compare
 *    objects by their pointers, e.g. (x == y).
 *
 * Author: Pavlo Manovi
 */

#ifndef MODEL_CPP_INTERFACE_H
#define MODEL_CPP_INTERFACE_H

#include "model_cpp.h"
#include "rdbfw.h"

#ifdef __cplusplus
extern "C" {
#endif

// Model virtual functions get their rdb c facing symbols defined here. 
extern Model* constructModel(const char *libName);

// Basic test method for this example project.
extern const int getName(Model *);
extern const void *getPreInitPtr(Model *);
extern const void *getInitPtr(Model *);
extern const void *getStartPtr(Model *);
extern const void *getStopPtr(Model *);
extern const void *getDeinitPtr(Model *);
extern const void *deliverMsg(Model *);

#ifdef __cplusplus
}
#endif

#endif /*MODEL_CPP_INTERFACE_H*/
