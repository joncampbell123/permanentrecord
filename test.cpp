
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#include <string>

///////////////////////////////////////////////////////

namespace PermanentRecord {

    enum IObjTypeId {
        IOI_IDontKnow = 0,
        IOI_IClockSource = 1
    };

    typedef int IObjRefcountType;

    typedef double timestamp_t;

}

///////////////////////////////////////////////////////

namespace PermanentRecord {

    template <class T> static inline T* stock_create_and_addref(void) {
        T* ret = new T();
        if (ret != NULL) ret->AddRef();
        return ret;
    }

}

///////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////

namespace PermanentRecord {

    /* Base class */
    class IDontKnow {
        public:
                                            IDontKnow(const IObjTypeId _type_id=IOI_IDontKnow);
            virtual                         ~IDontKnow();
        public:
            virtual IObjRefcountType        AddRef(void);
            virtual IObjRefcountType        Release(void);
            virtual bool                    QueryInterface(const enum IObjTypeId type_id,IDontKnow **ret);
            virtual void                    on_refcount_zero(void); // override if you want something other than "delete this"
        public:
            IObjTypeId                      object_type_id = IOI_IDontKnow;
            IObjRefcountType                refcount = 0;
            bool                            delete_on_refcount_zero = true;
        public:
            inline void                     DeleteOnRefcountZero(const bool en = true) {
                delete_on_refcount_zero = en;
            }
        public:
            static IDontKnow *Create(void) {
                return stock_create_and_addref<IDontKnow>();
            }
    };

}

///////////////////////////////////////////////////////

namespace PermanentRecord {

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
        if (type_id == object_type_id || type_id == IOI_IDontKnow) {
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

///////////////////////////////////////////////////////

namespace PermanentRecord {

    /* Base class */
    class IClockSource : public IDontKnow {
        public:
                                            IClockSource(const IObjTypeId _type_id=IOI_IClockSource);
            virtual                         ~IClockSource();
        public:
            virtual int                     UpdateClockValue(void) {
                return -ENOSYS;
///////////////////////////////////////////////////////

namespace PermanentRecord {

    /* return wall clock time */
    timestamp_t getWallClockTime(void) {
        return 0;
    }

}

            }
        public:
            std::string                     clock_name;
            timestamp_t                     clock_value = 0; /* in seconds */
        public:
            static IClockSource *Create(void) {
                return stock_create_and_addref<IClockSource>();
            }
    };

}

///////////////////////////////////////////////////////

namespace PermanentRecord {

    /* Base class */
    IClockSource::IClockSource(const IObjTypeId _type_id) : IDontKnow(_type_id) {
    }

    IClockSource::~IClockSource() {
        if (refcount != 0) LOG_MSG("WARNING: Object %p deleted with refcount %d",(void*)this,refcount);
    }

}

///////////////////////////////////////////////////////

using namespace PermanentRecord;

int main(int argc,char **argv) {
    (void)argc;
    (void)argv;

    LOG_MSG("Hello");
    LOG_MSG("Hello %u",123);

    {
        IDontKnow *val = IDontKnow::Create();

        {
            IDontKnow *v = NULL;
            if (val->QueryInterface(IOI_IDontKnow,&v)) {
                fprintf(stderr,"Yay\n");
                v->Release();
            }
        }

        val->Release();
    }

    {
        IClockSource *val = IClockSource::Create();

        {
            IDontKnow *v = NULL;
            if (val->QueryInterface(IOI_IDontKnow,&v)) {
                fprintf(stderr,"Yay2\n");
                v->Release();
            }
            if (val->QueryInterface(IOI_IClockSource,&v)) {
                fprintf(stderr,"Yay2\n");
                v->Release();
            }
        }

        val->Release();
    }

    return 0;
}

