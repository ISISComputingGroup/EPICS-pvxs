/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */
#ifndef DATAIMPL_H
#define DATAIMPL_H

#include <string>
#include <map>

#include <pvxs/data.h>
#include <pvxs/sharedArray.h>
#include <pvxs/bitmask.h>
#include "utilpvt.h"

namespace pvxs {
namespace impl {

struct ValueBase::Helper {
    // internal access to private operations
    static inline MValue build(const std::shared_ptr<const impl::FieldDesc>& desc) {
        return MValue(desc);
    }

    static inline       std::shared_ptr<impl::FieldStorage>& store(      ValueBase& v) { return v.store; }
    static inline std::shared_ptr<const impl::FieldStorage>  store(const ValueBase& v) { return v.store; }
    static inline const FieldDesc*                           desc(const ValueBase& v) { return v.desc; }

    static inline                       impl::FieldStorage*  store_ptr(      ValueBase& v) { return v.store.get(); }
    static inline                 const impl::FieldStorage*  store_ptr(const ValueBase& v) { return v.store.get(); }

    static std::shared_ptr<const impl::FieldDesc> type(const ValueBase& v);
};
} // namespace impl

namespace impl {
struct Buffer;

/** Describes a single field, leaf or otherwise, in a nested structure.
 *
 * FieldDesc are always stored depth first as a contigious array,
 * with offset to decendent fields given as positive integers relative
 * to the current field.  (not possible to jump _back_)
 *
 * We deal with two different numeric values:
 * 1. indicies in this FieldDesc array.  found in FieldDesc::mlookup and FieldDesc::miter
 *    Relative to current position in FieldDesc array.  (aka this+n)
 * 2. offsets in associated FieldStorage array.  found in FieldDesc::index
 *    Relative to current FieldDesc*.
 */
struct FieldDesc {
    // type ID string (Struct/Union)
    std::string id;

    // Lookup of all decendent fields of this Structure or Union.
    // "fld.sub.leaf" -> rel index
    // For Struct, relative to this
    // For Union, offset in members array
    std::map<std::string, size_t> mlookup;

    // child iteration.  child# -> ("sub", rel index in enclosing vector<FieldDesc>)
    std::vector<std::pair<std::string, size_t>> miter;

    // hash of this type (aggragating from children)
    // created using the code ^ id ^ (child_name ^ child_hash)*N
    size_t hash;

    // number of FieldDesc nodes between this node and it's a parent Struct (or 0 if no parent).
    // This value also appears in the parent's miter and mlookup mappings.
    // Only usable when a StructTop is accessible and this!=StructTop::desc
    size_t parent_index=0;

    // For Union, UnionA, StructA
    // For Union, the choices
    // For UnionA/StructA, size()==1 containing a Union/Struct
    std::vector<FieldDesc> members;

    TypeCode code{TypeCode::Null};

    // number of FieldDesc nodes which describe this node.  Inclusive.  always size()>=1
    inline size_t size() const { return 1u + (members.empty() ? mlookup.size() : 0u); }
};

PVXS_API
void to_wire(Buffer& buf, const FieldDesc* cur);

typedef std::map<uint16_t, std::vector<FieldDesc>> TypeStore;

PVXS_API
void from_wire(Buffer& buf, std::vector<FieldDesc>& descs, TypeStore& cache, unsigned depth=0);

struct StructTop;

struct FieldStorage {
    /* Storage for field value.  depends on StoreType.
     *
     * All array types stored as shared_array<const void> which includes full type info
     * Integers promoted to either int64_t or uint64_t.
     * Bool promoted to uint64_t
     * Reals promoted to double.
     * String stored as std::string
     * Compound (Struct, Union, Any) stored as Value
     */
    aligned_union<8,
                       double, // Real
                       uint64_t, // Bool, Integer
                       std::string, // String
                       IValue, // Union, Any
                       shared_array<const void> // array of POD, std::string, or std::shared_ptr<Value>
    >::type store;
    // index of this field in StructTop::members
    StructTop *top;
    bool valid=false;
    StoreType code=StoreType::Null;

    void init(const FieldDesc* desc);
    void deinit();
    ~FieldStorage();

    size_t index() const;

    template<typename T>
    T& as() { return *reinterpret_cast<T*>(&store); }
    template<typename T>
    const T& as() const { return *reinterpret_cast<const T*>(&store); }

    inline uint8_t* buffer() { return reinterpret_cast<uint8_t*>(&store); }
    inline const uint8_t* buffer() const { return reinterpret_cast<const uint8_t*>(&store); }
};

// hidden (publicly) management of an allocated Struct
struct StructTop {
    // type of first top level struct.  always !NULL.
    // Actually the first element of a vector<const FieldDesc>
    std::shared_ptr<const FieldDesc> desc;
    // our members (inclusive).  always size()>=1
    std::vector<FieldStorage> members;
};

using Type = std::shared_ptr<const FieldDesc>;


//! serialize all Value fields
PVXS_API
void to_wire_full(Buffer& buf, const ValueBase& val);

//! serialize BitMask and marked valid Value fields
PVXS_API
void to_wire_valid(Buffer& buf, const ValueBase& val, const BitMask* mask=nullptr);

//! deserialize type description
PVXS_API
void from_wire_type(Buffer& buf, TypeStore& ctxt, MValue& val);

//! deserialize full Value
PVXS_API
void from_wire_full(Buffer& buf, TypeStore& ctxt, MValue& val);

//! deserialize BitMask and partial Value
PVXS_API
void from_wire_valid(Buffer& buf, TypeStore& ctxt, MValue& val);

//! deserialize type description and full value (a la. pvRequest)
PVXS_API
void from_wire_type_value(Buffer& buf, TypeStore& ctxt, MValue& val);

PVXS_API
std::ostream& operator<<(std::ostream& strm, const FieldDesc* desc);

} // namespace impl


} // namespace pvxs

#endif // DATAIMPL_H
