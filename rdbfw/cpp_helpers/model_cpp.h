/* This model interface defines the virtual rdb interface which is
 * connected by a set of methods defined in model_cpp_interface.
 * If you decide to modify this interface, you will follow these two
 * rules:
 * 1. Thou shalt not try to directly access data contained within the
 *    c++ class which the Model pointer points to.
 * 2. Thou shalt not pass around Model pointers as void *'s, or compare
 *    objects by their pointers, e.g. (x == y).
 *
 *
 * In the CPP usermodel that implements / extends this interface, the
 * only unmangled symbol that needs to be provided is the constructor
 * bridge:
 *
 * extern "C" {
 * Model *constructCustomModelForC(void) {
 *     Model *mdl = new YourCustomUserModel;
 *     std::cout << "Constructed YourCustomUserModel object for C" << std::endl;
 *     return dynamic_cast<Model *>(mdl);
 * }
 * }
 *
 * Author: Pavlo Manovi
 */

#ifndef MODEL_CPP_H
#define MODEL_CPP_H

#ifdef __cplusplus
#include <iostream>
extern "C" {
    class Model {
    public:
        virtual ~Model() {};

        // Example function which will have each derived class print its name.
        virtual void printClassName(void) = 0;
        virtual void mdlPreInit(void *plugin) = 0;
        virtual void mdlInit(void) = 0;
        virtual void mdlStart(void) = 0;
        virtual void mdlStop(void) = 0;
        virtual void mdlDeinit(void) = 0;
        virtual void mdlHandleMsg(void) = 0;
};
}
#else
typedef
    struct Model
        Model;
#endif
#endif /*MODEL_CPP_H*/
