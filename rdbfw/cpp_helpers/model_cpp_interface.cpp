/* This model interface connects the virtual rdb interface to a C friendly
 * set of methods. If you decide to modify this interface, you will follow
 * these two rules:
 *
 * 1. Thou shalt not try to directly access data contained within the
 *    c++ class which the Model pointer points to.
 * 2. Thou shalt not pass around Model pointers as void *'s, or compare
 *    objects by their pointers, e.g. (x == y).
 *
 * Author: Pavlo Manovi
 */
#include <dlfcn.h>
#include <memory>
#include <iostream>
#include "model_cpp_interface.h"

static void skel_event_pre_init(void *p);
static void skel_event_init(void *p);
static void skel_event_start(void *p);
static void skel_event_stop(void *p);
static void skel_event_deinit(void *p);

typedef Model *(*ModelMakerPtr)();

// Model virtual functions get their rdb c facing symbols defined here. 
// It is very important that between compiler version updates that you convince
// yourself that this pointer mangling is behaving as expected.
Model* constructModel(const char *libName) {
    std::string libLoc;
    libLoc = libLoc + std::string(libName);
    std::cout << "Opening: " << libLoc << std::endl;

    // Open a cpp usermodel's library by name.
    ModelMakerPtr modelMaker;
    void *modelHandle = dlopen(libLoc.c_str(), RTLD_NOW | RTLD_LOCAL);

    if (modelHandle == nullptr) {
        std::cout << "Failed to open lib: " << dlerror() << std::endl;
	return nullptr;
    } else {
	std::cout << "Opened lib: " << modelHandle << std::endl;
    }

    // Can't assign a function pointer from dlsym as it returns void *,
    // but you /can/ reinterpret_cast what dlsym returns as the function
    // pointer we know this symbol will point to.
    void *test = dlsym(modelHandle, "constructCustomModelForC");
    if (test != nullptr) {
        modelMaker = reinterpret_cast<ModelMakerPtr (*)(void*, const char*)>(dlsym)(modelHandle, "constructCustomModelForC");
        std::cout << "custom modelMaker addr: " << modelMaker << " testAddr: " << test << std::endl;
    } else { 
        modelMaker = reinterpret_cast<ModelMakerPtr (*)(void*, const char*)>(dlsym)(modelHandle, "constructModelForC");
        std::cout << "modelMaker addr: " << modelMaker << " testAddr: " << test << std::endl;
    }
    Model *mdl = modelMaker();
    return mdl;
}

// rdb message call for constructor / start
extern const int getName(Model *mdl) {
    mdl->printClassName();
    return 0;
}

extern const void * getPreInitPtr(Model *mdl) {
    return (const void *) skel_event_pre_init;
}

extern const void * getInitPtr(Model *mdl) {
    return (const void *) skel_event_init;
}

extern const void * getStartPtr(Model *mdl) {
    return (const void *) skel_event_start;
}

extern const void * getStopPtr(Model *mdl) {
    return (const void *) skel_event_stop;
}

extern const void * getDeinitPtr(Model *mdl) {
    return (const void *) skel_event_deinit;
}

void handleMsg(Model *mdl) {
    mdl->mdlHandleMsg();
}

static void skel_event_pre_init(void *p) {
    plugins_t *ctx = (plugins_t *) p;
    ctx->mdl->mdlPreInit(p);
    return;
}

static void skel_event_init(void *p) {
    plugins_t *ctx = (plugins_t *) p;
    
    pthread_mutex_init(&ctx->msg_mutex, NULL);
    pthread_cond_init(&ctx->msg_condition, NULL);

    ctx->mdl->mdlInit();
 
    if (ctx->state == RDBFW_STATE_LOADED) {
        ctx->state = RDBFW_STATE_INITIALIZED;
    }
}

static void skel_event_start(void *p) {
    plugins_t *ctx = (plugins_t *) p;

    ctx->mdl->mdlStart();

    pthread_mutex_lock(&ctx->startup_mutex);
    if (ctx->state == RDBFW_STATE_INITIALIZED) {
        ctx->state = RDBFW_STATE_RUNNING;
    }
    pthread_mutex_unlock(&ctx->startup_mutex);
}

static void skel_event_stop(void *p) {
    plugins_t *ctx = (plugins_t *) p;
    int prev_state;
    
    prev_state = ctx->state;

    ctx->mdl->mdlStop();

    if (ctx->state == prev_state ) { 
        ctx->state = RDBFW_STATE_STOPPED;
    }
}

static void skel_event_deinit(void *p) {
    plugins_t *ctx = (plugins_t *) p;
    int prev_state;
    
    prev_state = ctx->state;

    ctx->mdl->mdlDeinit();
    if (ctx->state == prev_state ) { 
        ctx->state = RDBFW_STATE_LOADED;
    }
}
