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

NoField::NoField()
    :std::runtime_error ("No such field")
{}

NoField::~NoField() {}

NoConvert::NoConvert()
    :std::runtime_error ("No conversion defined")
{}

NoConvert::~NoConvert() {}

namespace impl {

std::shared_ptr<const impl::FieldDesc>
ValueBase::Helper::type(const ValueBase& v)
{
    if(v) {
        return std::shared_ptr<const impl::FieldDesc>(v.store->top->desc, v.desc);
    } else {
        return nullptr;
    }
}

ValueBase::ValueBase(const std::shared_ptr<const impl::FieldDesc>& desc)
    :desc(nullptr)
{
    if(!desc)
        return;

    auto top = std::make_shared<StructTop>();

    top->desc = desc;
    top->members.resize(desc->size());
    {
        auto& root = top->members[0];
        root.init(desc.get());
        root.top = top.get();
    }

    if(desc->code==TypeCode::Struct) {
        for(auto& pair : desc->mlookup) {
            auto cfld = desc.get() + pair.second;
            auto& mem = top->members.at(pair.second);
            mem.top = top.get();
            mem.init(cfld);
        }
    }

    this->desc = desc.get();
    decltype (store) val(top, top->members.data()); // alias
    this->store = std::move(val);
}

ValueBase::~ValueBase() {}

MValue ValueBase::cloneEmpty() const
{
    MValue ret;
    if(desc) {
        decltype (store->top->desc) fld(store->top->desc, desc);
        ret = MValue(fld);
    }
    return ret;
}

MValue ValueBase::clone() const
{
    MValue ret;
    if(desc) {
        decltype (store->top->desc) fld(store->top->desc, desc);
        ret = MValue(fld);
        ret.assign(*this);
    }
    return ret;
}

bool ValueBase::isMarked(bool parents, bool children) const
{
    if(!desc)
        return false;

    if(store->valid)
        return true;

    auto top = store->top;

    if(children && desc->size()>1u) {
        // TODO more efficient
        for(auto bit : range(desc->size()))
        {
            auto cstore = store.get() + bit;
            if(cstore->valid)
                return true;
        }
    }

    if(parents) {
        auto pdesc = desc;
        auto pstore = store.get();
        while(pdesc!=top->desc.get()) {
            pdesc -= pdesc->parent_index;
            pstore -= pdesc->parent_index;

            if(pstore->valid)
                return true;
        }
    }

    return false;
}

TypeCode ValueBase::type() const
{
    return desc ? desc->code : TypeCode::Null;
}

StoreType ValueBase::storageType() const
{
    return store ? store->code : StoreType::Null;
}

const std::string& ValueBase::id() const
{
    if(!desc)
        throw std::runtime_error("Null Value");
    return desc->id;
}

bool ValueBase::idStartsWith(const std::string& prefix) const
{
    auto ID = this->id();
    return ID.size()>=prefix.size() && prefix==ID.substr(0u, prefix.size());
}

const std::string &ValueBase::nameOf(const ValueBase& decendent) const
{
    if(!store || !decendent.store)
        throw NoField();
    auto pidx = store->index();
    auto didx = decendent.store->index();
    if(pidx >= didx || didx >= store->top->members.size())
        throw std::logic_error("not a decendent");

    // inefficient, but we don't keep a reverse mapping
    for(auto& it : desc->mlookup) {
        if(it.second == didx-pidx)
            return it.first;
    }

    throw std::logic_error("missing decendent");
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
    case StoreType::Bool:     *reinterpret_cast<bool*>(ptr) = src; return true;
    case StoreType::String:   *reinterpret_cast<std::string*>(ptr) = SB()<<src; return true;
    case StoreType::Null:
    case StoreType::Compound:
    case StoreType::Array:
        break;
    }
    return false;
}
}

void ValueBase::copyOut(void *ptr, StoreType type) const
{
    if(!desc)
        throw NoField();

    switch(store->code) {
    case StoreType::Real:     if(copyOutScalar(store->as<double>(), ptr, type)) return; else break;
    case StoreType::Integer:  if(copyOutScalar(store->as<int64_t>(), ptr, type)) return; else break;
    case StoreType::UInteger: if(copyOutScalar(store->as<uint64_t>(), ptr, type)) return; else break;
    case StoreType::Bool: {
        auto& src = store->as<bool>();

        switch(type) {
        case StoreType::Bool:     *reinterpret_cast<bool*>(ptr) = src; return;
        case StoreType::Integer:
        case StoreType::UInteger: *reinterpret_cast<uint64_t*>(ptr) = src; return;
        case StoreType::Real:     *reinterpret_cast<double*>(ptr) = src; return;
        case StoreType::String:   *reinterpret_cast<std::string*>(ptr) = src ? "true" : "false"; return;
        default:
            break;
        }
        break;
    }
    case StoreType::String: {
        auto& src = store->as<std::string>();

        switch(type) {
        case StoreType::String: *reinterpret_cast<std::string*>(ptr) = src; return;
        case StoreType::Integer: {
            epicsInt64 temp;
            if(epicsParseInt64(src.c_str(), &temp, 0, nullptr)==0) {
                *reinterpret_cast<int64_t*>(ptr) = temp;
                return;
            }
        }
        case StoreType::UInteger: {
            epicsUInt64 temp;
            if(epicsParseUInt64(src.c_str(), &temp, 0, nullptr)==0) {
                *reinterpret_cast<uint64_t*>(ptr) = temp;
                return;
            }
        }
        case StoreType::Real: {
            double temp;
            if(epicsParseDouble(src.c_str(), &temp, nullptr)==0) {
                *reinterpret_cast<double*>(ptr) = temp;
                return;
            }
        }
        case StoreType::Bool: {
            if(src=="true") { *reinterpret_cast<bool*>(ptr) = true; return; }
            else if(src=="flase") { *reinterpret_cast<bool*>(ptr) = false; return; }
        }
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
        auto& src = store->as<IValue>();
        if(type==StoreType::Compound) {
            // extract Value
            *reinterpret_cast<IValue*>(ptr) = src;
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

bool ValueBase::tryCopyOut(void *ptr, StoreType type) const
{
    try {
        copyOut(ptr, type);
        return true;
    }catch(NoField&){
        return false;
    }catch(NoConvert&){
        return false;
    }
}

void ValueBase::traverse(const std::string &expr, bool modify)
{
    size_t pos=0;
    while(desc && pos<expr.size()) {
        if(expr[pos]=='<') {
            // attempt traverse to parent
            if(desc!=store->top->desc.get())
            {
                auto pdesc = desc - desc->parent_index;
                std::shared_ptr<FieldStorage> pstore(store, store.get() - desc->parent_index);
                store = std::move(pstore);
                desc = pdesc;
                pos++;
                continue;
            } else {
                // at top
                store.reset();
                desc = nullptr;
                break;
            }

        }

        if(desc->code.code==TypeCode::Struct) {
            // attempt traverse to member.
            // expect: [0-9a-zA-Z_.]+[\[-$]
            size_t sep = expr.find_first_of("<[-", pos);

            decltype (desc->mlookup)::const_iterator it;

            if(sep>0 && (it=desc->mlookup.find(expr.substr(pos, sep-pos)))!=desc->mlookup.end()) {
                // found it
                auto next = desc+it->second;
                decltype(store) value(store, store.get()+it->second);
                store = std::move(value);
                desc = next;
                pos = sep;

            } else {
                // no such member
                store.reset();
                desc = nullptr;
            }

        } else if(modify) {
            // accessing a nested Union(A)/Any(A)/StructA as an IValue through a MValue would break const-ness
            store.reset();
            desc = nullptr;

        } else if(desc->code.code==TypeCode::Union || desc->code.code==TypeCode::Any) {
            // attempt to traverse to (and maybe select) member
            // expect: ->[0-9a-zA-Z_]+[.\[-$]

            if(expr.size()-pos >= 3 && expr[pos]=='-' && expr[pos+1]=='>') {
                pos += 2; // skip past "->"

                if(desc->code.code==TypeCode::Any) {
                    // select member of Any (may be Null)
                    *this = store->as<IValue>();

                } else {
                    // select member of Union
                    size_t sep = expr.find_first_of("<[-.", pos);

                    decltype (desc->mlookup)::const_iterator it;

                    if(sep>0 && (it=desc->mlookup.find(expr.substr(pos, sep-pos)))!=desc->mlookup.end()) {
                        // found it.
                        auto& fld = store->as<IValue>();

                        if(fld.desc==&desc->members[it->second]) {
                            // already selected
                            pos = sep;
                            *this = fld;

                        } else {
                            // traversing IValue, can't select Union
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
                shared_array<const IValue> arr;
                if((varr.original_type()==ArrayType::Value)
                        && index < (arr = varr.castTo<const IValue>()).size())
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

void ValueBase::_iter_fl(ValueBase::IterInfo &info, bool first) const
{
    if(!store)
        throw NoField();

    if(info.depth) {
        info.pos = info.nextcheck = store->index() + (first ? 1u : desc->size());

        if(info.marked)
            _iter_advance(info);

    } else {
        info.pos = info.nextcheck = first ? 0u : desc->miter.size();
    }
}

void ValueBase::_iter_advance(IterInfo& info) const
{
    assert(info.depth);

    // scan forward to find next non-marked
    for(auto idx : range(info.pos, desc->size())) {
        auto S = store.get() + idx;
        if(S->valid) {
            auto D = desc + idx;
            info.pos = idx;
            info.nextcheck = idx + D->size();
            return;
        }
    }

    info.pos = info.nextcheck = desc->size();
}

void ValueBase::_iter_deref(const IterInfo& info, ValueBase& ret) const
{
    auto idx = info.pos;
    if(!info.depth)
        idx = desc->miter[idx].second;

    decltype (store) store2(store, store.get()+idx);

    ret.store = std::move(store2);
    ret.desc = desc + idx;
}

} // namespace impl

static
void show_Value(std::ostream& strm,
                const std::string& member,
                const ValueBase& val,
                unsigned level=0);

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
                show_Value(strm, pair.first, cdesc, store + pair.second, level+1);
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
    case StoreType::Bool: strm<<" = "<<(store->as<bool>() ? "true" : "false")<<"\n"; break;
    case StoreType::String:   strm<<" = \""<<escape(store->as<std::string>())<<"\"\n"; break;
    case StoreType::Compound: {
        auto& fld = store->as<IValue>();
        if(fld.valid() && desc->code==TypeCode::Union) {
            for(auto& pair : desc->miter) {
                if(&desc->members[pair.second] == ValueBase::Helper::desc(fld)) {
                    strm<<"."<<pair.first;
                    break;
                }
            }
        }
        show_Value(strm, std::string(), fld, level+1);
    }
        break;
    case StoreType::Array: {
        auto& varr = store->as<shared_array<const void>>();
        if(varr.original_type()!=ArrayType::Value) {
            strm<<" = "<<varr<<"\n";
        } else {
            auto arr = varr.castTo<const IValue>();
            strm<<" [\n";
            for(auto& val : arr) {
                show_Value(strm, std::string(), val, level+1);
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

static
void show_Value(std::ostream& strm,
                const std::string& member,
                const ValueBase& val,
                unsigned level)
{
    show_Value(strm, member,
               ValueBase::Helper::desc(val),
               ValueBase::Helper::store_ptr(val),
               level);
}

std::ostream& operator<<(std::ostream& strm, const ValueBase& val)
{
    show_Value(strm, std::string(), val);
    return strm;
}

IValue MValue::freeze()
{
    if(store.use_count()>1u)
        throw std::runtime_error(SB()<<"Can't freeze MValue with "<<store.use_count()<<" refs");

    IValue ret;
    ret.store = std::move(store);
    ret.desc = desc;
    desc = nullptr;
    return ret;
}

MValue& MValue::assign(const ValueBase& o)
{
    if(desc!=o.desc &&
            desc->code==o.desc->code &&
            (desc->code.kind()==Kind::Integer
             || desc->code.kind()==Kind::Real
             || desc->code.kind()==Kind::String
             || desc->code.kind()==Kind::Bool))
    {
        // allow simple fields

    } else if(desc!=o.desc) {
         // TODO relax
        throw std::runtime_error("Can only assign same TypeDef");
    }

    if(desc) {
        for(size_t bit=0, end=desc->size(); bit<end;) {
            auto sstore = o.store.get() + bit;
            auto dstore = store.get() + bit;

            if(!sstore->valid) {
                bit++;
                continue;
            }

            dstore->valid = true;

            switch(dstore->code) {
            case StoreType::Real:
            case StoreType::Bool:
            case StoreType::Integer:
            case StoreType::UInteger:
                dstore->as<uint64_t>() = sstore->as<uint64_t>();
                bit++;
                break;
            case StoreType::String:
                dstore->as<std::string>() = sstore->as<std::string>();
                bit++;
                break;
            case StoreType::Array:
                dstore->as<shared_array<const void>>() = sstore->as<shared_array<const void>>();
                bit++;
                break;
            case StoreType::Compound:
                dstore->as<IValue>() = sstore->as<IValue>();
                bit++;
                break;
            case StoreType::Null: {
                // copy entire sub-structure
                auto sdesc = desc + bit;

                for(auto end2 = bit + sdesc->size(); bit<end2; bit++)
                {
                    auto sstore = o.store.get() + bit;
                    auto dstore = store.get() + bit;

                    dstore->valid = true;

                    switch(dstore->code) {
                    case StoreType::Real:
                    case StoreType::Bool:
                    case StoreType::Integer:
                    case StoreType::UInteger:
                        dstore->as<uint64_t>() = sstore->as<uint64_t>();
                        bit++;
                        break;
                    case StoreType::String:
                        dstore->as<std::string>() = sstore->as<std::string>();
                        bit++;
                        break;
                    case StoreType::Array:
                        dstore->as<shared_array<const void>>() = sstore->as<shared_array<const void>>();
                        bit++;
                        break;
                    case StoreType::Compound:
                        dstore->as<IValue>() = sstore->as<IValue>();
                        bit++;
                        break;
                    case StoreType::Null: // skip sub-struct nodes, we will copy all leaf nodes
                        break;
                    }

                }
            }
                break;
            }
        }
    }
    return *this;
}

MValue MValue::allocMember() const
{
    // allocate member type for Struct[] or Union[]
    if(!desc || (desc->code!=TypeCode::UnionA && desc->code!=TypeCode::StructA))
        throw std::runtime_error("allocMember() only meaningful for Struct[] or Union[]");

    decltype (store->top->desc) fld(store->top->desc, desc->members.data());
    return ValueBase::Helper::build(fld);
}

void MValue::mark(bool v)
{
    if(!desc)
        return;

    store->valid = v;
}

void MValue::unmark(bool parents, bool children)
{
    if(!desc)
        return;

    store->valid = false;

    auto top = store->top;

    if(children && desc->size()>1u) {
        // TODO more efficient
        for(auto bit : range(desc->size()))
        {
            (store.get() + bit)->valid = false;
        }
    }

    if(parents) {
        auto pdesc = desc;
        auto pstore = store.get();
        while(pdesc!=top->desc.get()) {
            pdesc -= pdesc->parent_index;
            pstore -= pdesc->parent_index;

            pstore->valid = false;
        }
    }
}

MValue IValue::thaw()
{
    MValue ret;
    if(store.use_count()==1u) {
        ret.store = std::move(store);
        ret.desc = desc;
        desc = nullptr;
        return ret;
    } else {
        ret = clone();
    }
    return ret;
}

namespace {

bool parseScalar(double& dest, const std::string& inp)
{
    return 0==epicsParseDouble(inp.c_str(), &dest, nullptr);
}

bool parseScalar(int64_t& dest, const std::string& inp)
{
    epicsInt64 t;
    if(epicsParseInt64(inp.c_str(), &t, 0, nullptr)==0) {
        dest = t;
        return true;
    }
    return false;
}

bool parseScalar(uint64_t& dest, const std::string& inp)
{
    epicsUInt64 t;
    if(epicsParseUInt64(inp.c_str(), &t, 0, nullptr)==0) {
        dest = t;
        return true;
    }
    return false;
}

// C-style cast between scalar storage types, and print to string (base 10)
template<typename Dest>
bool copyInScalar(Dest& dest, const void *ptr, StoreType type)
{
    switch(type) {
    case StoreType::Real:     dest = *reinterpret_cast<const double*>(ptr); return true;
    case StoreType::Integer:  dest = *reinterpret_cast<const int64_t*>(ptr); return true;
    case StoreType::UInteger: dest = *reinterpret_cast<const uint64_t*>(ptr); return true;
    case StoreType::Bool:     dest = *reinterpret_cast<const bool*>(ptr); return true;
    case StoreType::String:   return parseScalar(dest, *reinterpret_cast<const std::string*>(ptr));
    case StoreType::Null:
    case StoreType::Compound:
    case StoreType::Array:
        break;
    }
    return false;
}
}

void MValue::copyIn(const void *ptr, StoreType type)
{
    // control flow should either throw NoField or NoConvert, or update 'store' and
    // reach the mark() at the end.

    if(!desc)
        throw NoField();

    switch(store->code) {
    case StoreType::Real:     if(!copyInScalar(store->as<double>(), ptr, type)) throw NoConvert(); break;
    case StoreType::Integer:  if(!copyInScalar(store->as<int64_t>(), ptr, type)) throw NoConvert(); break;
    case StoreType::UInteger: if(!copyInScalar(store->as<uint64_t>(), ptr, type)) throw NoConvert(); break;
    case StoreType::Bool: {
        auto& dest = store->as<bool>();
        switch(type) {
        case StoreType::Bool:     dest = *reinterpret_cast<const bool*>(ptr); break;
        case StoreType::Integer:
        case StoreType::UInteger: dest = 0!=*reinterpret_cast<const uint64_t*>(ptr); break;
        //case StoreType::Real:  // TODO pick condition.  strict non-zero?  fabs()<0.5 ?
        case StoreType::String:
            if("true"==*reinterpret_cast<const std::string*>(ptr)) { dest = true; break; }
            else if("false"==*reinterpret_cast<const std::string*>(ptr)) { dest = false; break; }
            // fall through
        default:
            throw NoConvert();
        }
        break;
    }
    case StoreType::String: {
        auto& dest = store->as<std::string>();

        switch(type) {
        case StoreType::String:   dest = *reinterpret_cast<const std::string*>(ptr); break;
        case StoreType::Integer:  dest = SB()<<*reinterpret_cast<const int64_t*>(ptr); break;
        case StoreType::UInteger: dest = SB()<<*reinterpret_cast<const uint64_t*>(ptr); break;
        case StoreType::Real:     dest = SB()<<*reinterpret_cast<const double*>(ptr); break;
        case StoreType::Bool:     dest = (*reinterpret_cast<const bool*>(ptr)) ? "true" : "false"; break;
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
                auto tsrc  = src.castTo<const IValue>();

                if(desc->code!=TypeCode::AnyA) {
                    // enforce member type for Struct[] and Union[]
                    for(auto& val : tsrc) {
                        if(val.desc && val.desc!=desc->members.data()) {
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
                store->as<IValue>() = *reinterpret_cast<const IValue*>(ptr);
                break;
            }
        }
        throw NoConvert();
    case StoreType::Null:
        throw NoConvert();
    }

    mark();
}

bool MValue::tryCopyIn(const void *ptr, StoreType type)
{
    try {
        copyIn(ptr, type);
        return true;
    }catch(NoField&){
        return false;
    }catch(NoConvert&){
        return false;
    }
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
            new(&store) IValue();
            this->code = StoreType::Compound;
            break;
        case Kind::Integer:
            as<uint64_t>() = 0u;
            this->code = desc->code.isunsigned() ? StoreType::UInteger : StoreType::Integer;
            break;
        case Kind::Bool:
            as<bool>() = false;
            this->code = StoreType::Bool;
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
    case StoreType::Bool:
             break;
    case StoreType::Array:
        as<shared_array<void>>().~shared_array();
        break;
    case StoreType::String:
        as<std::string>().~basic_string();
        break;
    case StoreType::Compound:
        as<IValue>().~IValue();
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
    return ret;
}

}} // namespace pvxs::impl
