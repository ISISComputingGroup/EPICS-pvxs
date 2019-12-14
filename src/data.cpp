/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */

#include <cstring>
#include <epicsAssert.h>

#include <epicsStdlib.h>

#include "dataimpl.h"
#include "utilpvt.h"

namespace pvxs {

NoConvert::NoConvert()
    :std::runtime_error ("No conversion defined")
{}

NoConvert::~NoConvert() {}

Value::Value(const std::shared_ptr<const impl::FieldDesc>& desc)
    :desc(nullptr)
{
    if(!desc)
        return;

    auto top = std::make_shared<StructTop>();

    top->desc = desc;
    top->valid.resize(desc->next_offset-desc->offset);
    top->member_indicies.resize(top->valid.size());
    top->members.resize(top->valid.size());
    {
        auto& root = top->members[0];
        root.init(desc.get());
        root.top = top.get();
        top->member_indicies[0u] = 0u;
    }

    for(auto& pair : desc->mlookup) {
        auto cfld = desc.get() + pair.second;
        auto& mem = top->members.at(cfld->offset-desc->offset);
        mem.top = top.get();
        mem.init(cfld);
        top->member_indicies[cfld->offset] = pair.second;
    }

    this->desc = desc.get();
    decltype (store) val(top, top->members.data()); // alias
    this->store = std::move(val);
}

Value::~Value() {}

Value Value::cloneEmpty() const
{
    Value ret;
    if(desc) {
        decltype (store->top->desc) fld(store->top->desc, desc);
        ret = Value(fld);
    }
    return ret;
}

Value Value::clone() const
{
    Value ret;
    if(desc) {
        decltype (store->top->desc) fld(store->top->desc, desc);
        ret = Value(fld);
        //ret.assign(*this);
    }
    return ret;
}

//Value& Value::assign(const Value& o)
//{
//    if(desc!=o.desc)
//        throw std::runtime_error("Can only assign same TypeDef"); // TODO relax

//    return *this;
//}

Value Value::allocMember() const
{
    // allocate member type for Struct[] or Union[]
    if(!desc || (desc->code!=TypeCode::UnionA && desc->code!=TypeCode::StructA))
        throw std::runtime_error("allocMember() only meaningful for Struct[] or Union[]");

    decltype (store->top->desc) fld(store->top->desc, desc+1);
    return Value(fld);
}

bool Value::isMarked(bool parents, bool children) const
{
    // TODO test parent and child mask
    return desc ? store->top->valid[store->index()] : false;
}

void Value::mark(bool v)
{
    if(desc)
        store->top->valid[store->index()] = v;
}

void Value::unmark(bool parents, bool children)
{
    // TODO clear parent and/or child mask
    if(desc)
        store->top->valid[store->index()] = false;
}

TypeCode Value::type() const
{
    return desc ? desc->code : TypeCode::Null;
}

StoreType Value::storageType() const
{
    return store ? store->code : StoreType::Null;
}

const std::string& Value::id() const
{
    if(!desc)
        throw std::runtime_error("Null Value");
    return desc->id;
}

namespace {
// C-style cast between scalar storage types, and print to string (base 10)
template<typename Src>
bool copyOutScalar(const Src& src, void *ptr, StoreType type)
{
    switch(type) {
    case StoreType::Real:     *reinterpret_cast<double*>(ptr) = src; return true;
    case StoreType::Integer:  *reinterpret_cast<int64_t*>(ptr) = src; return true;
    case StoreType::UInteger: *reinterpret_cast<uint64_t*>(ptr) = src; return true;
    case StoreType::String:   *reinterpret_cast<std::string*>(ptr) = SB()<<src; return true;
    case StoreType::Null:
    case StoreType::Compound:
    case StoreType::Array:
        break;
    }
    return false;
}
}

void Value::copyOut(void *ptr, StoreType type) const
{
    if(!desc)
        throw NoConvert();

    switch(store->code) {
    case StoreType::Real:     if(copyOutScalar(store->as<double>(), ptr, type)) return; else break;
    case StoreType::Integer:  if(copyOutScalar(store->as<int64_t>(), ptr, type)) return; else break;
    case StoreType::UInteger: if(copyOutScalar(store->as<uint64_t>(), ptr, type)) return; else break;
    case StoreType::String: {
        auto& src = store->as<std::string>();

        switch(type) {
        case StoreType::String: *reinterpret_cast<std::string*>(ptr) = src; return;
        // TODO: parse Integer/Real
        default:
            break;
        }
        break;
    }
    case StoreType::Array: {
        auto& src = store->as<shared_array<const void>>();
        switch (type) {
        case StoreType::Array: *reinterpret_cast<shared_array<const void>*>(ptr) = src; return;
            // TODO: print array
            //       extract [0] as scalar?
        default:
            break;
        }
        break;
    }
    case StoreType::Compound: {
        auto& src = store->as<Value>();
        if(type==StoreType::Compound) {
            // extract Value
            *reinterpret_cast<Value*>(ptr) = src;
            return;

        } else if(src) {
            // automagic deref and delegate assign
            src.copyOut(ptr, type);
            return;

        } else {
            throw NoConvert();
        }

        break;
    }
    case StoreType::Null:
        break;
    }

    throw NoConvert();
}

namespace {
// C-style cast between scalar storage types, and print to string (base 10)
template<typename Dest>
bool copyInScalar(Dest& dest, const void *ptr, StoreType type)
{
    switch(type) {
    case StoreType::Real:     dest = *reinterpret_cast<const double*>(ptr); return true;
    case StoreType::Integer:  dest = *reinterpret_cast<const int64_t*>(ptr); return true;
    case StoreType::UInteger: dest = *reinterpret_cast<const uint64_t*>(ptr); return true;
    case StoreType::String: // TODO: parse from string
    case StoreType::Null:
    case StoreType::Compound:
    case StoreType::Array:
        break;
    }
    return false;
}
}

void Value::copyIn(const void *ptr, StoreType type)
{
    if(!desc)
        throw NoConvert();

    switch(store->code) {
    case StoreType::Real:     if(!copyInScalar(store->as<double>(), ptr, type)) throw NoConvert(); break;
    case StoreType::Integer:  if(!copyInScalar(store->as<int64_t>(), ptr, type)) throw NoConvert(); break;
    case StoreType::UInteger: if(!copyInScalar(store->as<uint64_t>(), ptr, type)) throw NoConvert(); break;
    case StoreType::String: {
        auto& dest = store->as<std::string>();

        switch(type) {
        case StoreType::String:   dest = *reinterpret_cast<const std::string*>(ptr); break;
        case StoreType::Integer:  dest = SB()<<*reinterpret_cast<const int64_t*>(ptr); break;
        case StoreType::UInteger: dest = SB()<<*reinterpret_cast<const uint64_t*>(ptr); break;
        case StoreType::Real:     dest = SB()<<*reinterpret_cast<const double*>(ptr); break;
        default:
            throw NoConvert();
        }
        break;
    }
    case StoreType::Array: {
        auto& dest = store->as<shared_array<const void>>();
        switch (type) {
        case StoreType::Array: {
            auto& src = *reinterpret_cast<const shared_array<const void>*>(ptr);
            if(src.original_type()==ArrayType::Null || src.empty()) {
                // assignment from untyped or empty
                dest.clear();

            } else if(src.original_type()==ArrayType::Value && desc->code.kind()==Kind::Compound) {
                // assign array of Struct/Union/Any
                auto tsrc  = shared_array_static_cast<const Value>(src);

                if(desc->code!=TypeCode::AnyA) {
                    // enforce member type for Struct[] and Union[]
                    for(auto& val : tsrc) {
                        if(val.desc && val.desc!=desc+1) {
                            throw NoConvert();
                        }
                    }
                }
                dest = src;

            } else if(src.original_type()!=ArrayType::Value && uint8_t(desc->code.code)==uint8_t(src.original_type())) {
                // assign array of scalar w/o convert
                dest = src;

            } else {
                // TODO: alloc and convert
                throw NoConvert();
            }
            break;
        }
        default:
            throw NoConvert();
        }
        break;
    }
    case StoreType::Compound:
        if(desc->code==TypeCode::Any) {
            // assigning variant union.
            if(type==StoreType::Compound) {
                store->as<Value>() = *reinterpret_cast<const Value*>(ptr);
                break;
            }
        }
        // fall through
    case StoreType::Null:
        throw NoConvert();
    }

    store->top->valid[store->index()] = true;
}

void Value::traverse(const std::string &expr, bool modify)
{
    size_t pos=0;
    while(desc && pos<expr.size()) {

        if(desc->code.code==TypeCode::Struct) {
            // attempt traverse to member.
            // expect: [0-9a-zA-Z_.]+[\[-$]
            size_t sep = expr.find_first_of("[-", pos);

            decltype (desc->mlookup)::const_iterator it;

            if(sep>0 && (it=desc->mlookup.find(expr.substr(pos, sep-pos)))!=desc->mlookup.end()) {
                // found it
                auto next = desc+it->second;
                auto offset = next->offset - desc->offset;
                decltype(store) value(store, store.get()+offset);
                store = std::move(value);
                desc = next;
                pos = sep;

            } else {
                // no such member
                store.reset();
                desc = nullptr;
            }

        } else if(desc->code.code==TypeCode::Union || desc->code.code==TypeCode::Any) {
            // attempt to traverse to (and maybe select) member
            // expect: ->[0-9a-zA-Z_]+[.\[-$]

            if(expr.size()-pos >= 3 && expr[pos]=='-' && expr[pos+1]=='>') {
                pos += 2; // skip past "->"

                if(desc->code.code==TypeCode::Any) {
                    // select member of Any (may be Null)
                    *this = store->as<Value>();

                } else {
                    // select member of Union
                    size_t sep = expr.find_first_of("[-.", pos);

                    decltype (desc->mlookup)::const_iterator it;

                    if(sep>0 && (it=desc->mlookup.find(expr.substr(pos, sep-pos)))!=desc->mlookup.end()) {
                        // found it.
                        auto& fld = store->as<Value>();

                        if(modify || fld.desc==desc+it->second) {
                            // will select, or already selected
                            if(fld.desc!=desc+it->second) {
                                // select
                                std::shared_ptr<const FieldDesc> mtype(store->top->desc, desc+it->second);
                                fld = Value(mtype);
                            }
                            pos = sep;
                            *this = fld;

                        } else {
                            // traversing const Value, can't select Union
                            store.reset();
                            desc = nullptr;
                        }
                    }
                }
            } else {
                // expected "->"
                store.reset();
                desc = nullptr;
            }

        } else if(desc->code.isarray() && desc->code.kind()==Kind::Compound) {
            // attempt to traverse into array of Struct, Union, or Any
            // expect: \[[0-9]+\]

            size_t sep = expr.find_first_of(']', pos);
            unsigned long long index=0;

            if(expr[pos]=='['
                    && sep!=std::string::npos && sep-pos>=2
                    && !epicsParseULLong(expr.substr(pos+1, sep-1-pos).c_str(), &index, 0, nullptr))
            {
                auto& varr = store->as<shared_array<const void>>();
                shared_array<const Value> arr;
                if((varr.original_type()==ArrayType::Value)
                        && index < (arr = shared_array_static_cast<const Value>(varr)).size())
                {
                    *this = arr[index];
                    pos = sep+1;
                } else {
                    // wrong element type or out of range
                    store.reset();
                    desc = nullptr;
                }

            } else {
                // syntax error
                store.reset();
                desc = nullptr;
            }

        } else {
            // syntax error or wrong field type (can't index scalar array)
            store.reset();
            desc = nullptr;
        }
    }
}

Value Value::operator[](const char *name)
{
    Value ret(*this);
    ret.traverse(name, true);
    return ret;
}

const Value Value::operator[](const char *name) const
{
    Value ret(*this);
    ret.traverse(name, false);
    return ret;
}

static
void show_Value(std::ostream& strm,
                const std::string& member,
                const FieldDesc *desc,
                const FieldStorage* store,
                unsigned level=0)
{
    indent(strm, level);
    if(!desc) {
        strm<<"null\n";
        return;
    }

    strm<<desc->code;
    if(!desc->id.empty())
        strm<<" \""<<desc->id<<"\"";
    if(!member.empty() && desc->code!=TypeCode::Struct)
        strm<<" "<<member;

    switch(store->code) {
    case StoreType::Null:
        if(desc->code==TypeCode::Struct) {
            strm<<" {\n";
            for(auto& pair : desc->miter) {
                auto cdesc = desc + pair.second;
                show_Value(strm, pair.first, cdesc, store - desc->offset + cdesc->offset, level+1);
            }
            indent(strm, level);
            strm<<"}";
            if(!member.empty())
                strm<<" "<<member;
            strm<<"\n";
        } else {
            strm<<"\n";
        }
        break;
    case StoreType::Real:     strm<<" = "<<store->as<double>()<<"\n"; break;
    case StoreType::Integer:  strm<<" = "<<store->as<int64_t>()<<"\n"; break;
    case StoreType::UInteger: strm<<" = "<<store->as<uint64_t>()<<"\n"; break;
    case StoreType::String:   strm<<" = \""<<escape(store->as<std::string>())<<"\"\n"; break;
    case StoreType::Compound: {
        auto& fld = store->as<Value>();
        if(fld.valid() && desc->code==TypeCode::Union) {
            for(auto& pair : desc->miter) {
                if(desc + pair.second == fld._desc()) {
                    strm<<"."<<pair.first;
                    break;
                }
            }
        }
        show_Value(strm, std::string(), fld._desc(), fld._store(), level+1);
    }
        break;
    case StoreType::Array: {
        auto& varr = store->as<shared_array<const void>>();
        if(varr.original_type()!=ArrayType::Value) {
            strm<<" = "<<varr<<"\n";
        } else {
            auto arr = shared_array_static_cast<const Value>(varr);
            strm<<" [\n";
            for(auto& val : arr) {
                show_Value(strm, std::string(), val._desc(), val._store(), level+1);
            }
            indent(strm, level);
            strm<<"]\n";
        }
    }
        break;
    default:
        strm<<"!!Invalid StoreType!! "<<int(store->code)<<"\n";
        break;
    }
}

std::ostream& operator<<(std::ostream& strm, const Value& val)
{
    show_Value(strm, std::string(), val._desc(), val._store());
    return strm;
}

namespace impl {

void FieldStorage::init(const FieldDesc *desc)
{
    if(!desc || desc->code.kind()==Kind::Null || desc->code.code==TypeCode::Struct) {
        this->code = StoreType::Null;

    } else if(desc->code.isarray()) {
        this->code = StoreType::Array;
        new(&store) shared_array<void>();

    } else {
        switch(desc->code.kind()) {
        case Kind::String:
            new(&store) std::string();
            this->code = StoreType::String;
            break;
        case Kind::Compound:
            new(&store) std::shared_ptr<FieldStorage>();
            this->code = StoreType::Compound;
            break;
        case Kind::Integer:
            if(!desc->code.isunsigned()) {
                as<int64_t>() = 0u;
                this->code = StoreType::Integer;
                break;
            }
            // fall trhough
        case Kind::Bool:
            as<uint64_t>() = 0u;
            this->code = StoreType::UInteger;
            break;
        case Kind::Real:
            as<double>() = 0.0;
            this->code = StoreType::Real;
            break;
        default:
            throw std::logic_error("FieldStore::init()");
        }
    }
}

void FieldStorage::deinit()
{
    switch(code) {
    case StoreType::Null:
    case StoreType::Integer:
    case StoreType::UInteger:
    case StoreType::Real:
             break;
    case StoreType::Array:
        as<shared_array<void>>().~shared_array();
        break;
    case StoreType::String:
        as<std::string>().~basic_string();
        break;
    case StoreType::Compound:
        as<Value>().~Value();
        break;
    default:
        throw std::logic_error("FieldStore::deinit()");
    }
    code = StoreType::Null;
}

FieldStorage::~FieldStorage()
{
    deinit();
}

size_t FieldStorage::index() const
{
    const size_t ret = this-top->members.data();
    assert(this==&top->members[ret]);
    return ret;
}

}} // namespace pvxs::impl