
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
            virtual bool                    QueryInterface(const enum IObjTypeId type_id,IDontKnow **ret);
            virtual void                    on_refcount_zero(void); // override if you want something other than "delete this"
        public:
            IObjTypeId                      object_type_id = IOI_NONE;
            IObjRefcountType                refcount = 0;
            bool                            delete_on_refcount_zero = false;
        public:
            inline void                     DeleteOnRefcountZero(const bool en = true) {
                delete_on_refcount_zero = en;
            }
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

    /* Base class */
    IDontKnow::IDontKnow(const IObjTypeId _type_id) : object_type_id(_type_id) {
    }

    IDontKnow::~IDontKnow() {
        if (refcount != 0) LOG_MSG("WARNING: Object %p deleted with refcount %d",(void*)this,refcount);
    }

    IObjRefcountType IDontKnow::AddRef(void) {
        return ++refcount;
    }

    IObjRefcountType IDontKnow::Release(void) {
        // NTS: Decrement, store to stack storage.
        //      If this function deletes *this on refcount == 0 we'll
        //      still have a valid value to return.
        const IObjRefcountType ret = --refcount;

        if (ret < 0) LOG_MSG("WARNING: Object %p refcount negative (%d)",(void*)this,refcount);
        if (ret == 0) on_refcount_zero();

        return ret;
    }

    bool IDontKnow::QueryInterface(const enum IObjTypeId type_id,IDontKnow **ret) {
        if (type_id == object_type_id) {
            AddRef();
            *ret = static_cast<IDontKnow*>(this);
            return true;
        }

        return false;
    }

    void IDontKnow::on_refcount_zero(void) {
        // Default on refcount == 0
        if (delete_on_refcount_zero)
            delete this;
    }

}

int main(int argc,char **argv) {
    (void)argc;
    (void)argv;

    PermanentRecord::LOG_MSG("Hello");
    PermanentRecord::LOG_MSG("Hello %u",123);

    {
        PermanentRecord::IDontKnow val(PermanentRecord::IOI_NONE);
        val.AddRef();

        {
            PermanentRecord::IDontKnow *v = NULL;
            if (val.QueryInterface(PermanentRecord::IOI_NONE,&v)) {
                fprintf(stderr,"Yay\n");
                v->Release();
            }
        }

        val.Release();
    }

    return 0;
}

