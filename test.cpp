
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

namespace PermanentRecord {

    enum IObjTypeId {
        IOI_NONE = 0
    };

    typedef int IObjRefcountType;

    void LOG_MSG(const char *fmt,...);

    /* Base class */
    class IDontKnow {
        public:
                                            IDontKnow(const IObjTypeId _type_id);
            virtual                         ~IDontKnow();
        public:
            virtual IObjRefcountType        AddRef(void);
            virtual IObjRefcountType        Release(void);
        public:
            IObjTypeId                      object_type_id = IOI_NONE;
            IObjRefcountType                refcount = 1;
    };

}

namespace PermanentRecord {

static char LOG_TMP[1024];

void LOG_MSG_callback_stderr(const char * const str) {
    fprintf(stderr,"PR::LOG_MSG: %s\n",str);
}

void (*LOG_MSG_callback)(const char * const str) = LOG_MSG_callback_stderr;

void LOG_MSG(const char * const fmt,...) {
    va_list va;

    va_start(va,fmt);
    LOG_TMP[sizeof(LOG_TMP)-1] = 0;
    vsnprintf(LOG_TMP,sizeof(LOG_TMP)-1,fmt,va);
    va_end(va);

    if (LOG_MSG_callback != NULL)
        LOG_MSG_callback(LOG_TMP);
}

}

int main(int argc,char **argv) {
    (void)argc;
    (void)argv;

    PermanentRecord::LOG_MSG("Hello");
    PermanentRecord::LOG_MSG("Hello %u",123);

    return 0;
}

