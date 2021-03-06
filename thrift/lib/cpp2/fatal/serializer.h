/*
 * Copyright 2016 Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <thrift/lib/cpp2/fatal/reflection.h>
#include <thrift/lib/cpp2/fatal/container_traits.h>
#include <vector>
#include <array>
#include <iostream>
#include <type_traits>
#include <iterator>

#include <fatal/type/string_lookup.h>
#include <fatal/type/call_traits.h>
#include <fatal/type/map.h>

namespace apache { namespace thrift {

namespace detail {

inline bool is_unknown_container_size(uint32_t const size) {
  return size == std::numeric_limits<decltype(size)>::max();
}

} // namespace detail {

/**
 * Specializations of `protocol_methods` encapsulate a collection of
 * read/write/size/sizeZC methods that can be performed on Thrift
 * objects and primitives. TypeClass (see apache::thrift::type_class)
 * refers to the general type of data that Type is, and is passed around for
 * two reasons:
 *  - to provide support for generic containers which have a common interface
 *    for building collections (e.g. a `std::vector<int>` and `std::deque<int>`,
 *    which can back a Thrift list, and thus have
 *    `type_class::list<type_class::integral>`, or an
 *    `std::map<std::string, MyStruct>` would have
 *    `type_class::map<type_class::string, type_class::structure>``).
 *  - to differentiate between Thrift types that are represented with the
 *    same C++ type, e.g. both Thrift binaries and strings are represented
 *    with an `std::string`, TypeClass is used to decide which protocol
 *    method to call.
 *
 * Example:
 *
 * // MyModule.thrift:
 * struct MyStruct {
 *   1: i32 fieldA
 *   2: string fieldB
 *   3: binary fieldC
 * }
 *
 * // C++
 *
 * using methods = protocol_methods<type_class::structure, MyStruct>;
 *
 * MyStruct struct_instance;
 * CompactProtocolReader reader;
 * methods::read(struct_instance, reader);
 *
 * @author: Dylan Knutson <dymk@fb.com>
 */
template <typename TypeClass, typename Type, typename Enable = void>
struct protocol_methods;

#define REGISTER_OVERLOAD_COMMON(Class, Type, Method, TTypeValue) \
  constexpr static protocol::TType ttype_value = protocol::TTypeValue; \
  template <typename Protocol>              \
  static std::size_t read(Protocol& protocol, Type& out) {       \
    DVLOG(3) << "read primitive " << #Class << ": " << #Type; \
    return protocol.read##Method(out); \
  } \
  template <typename Protocol>              \
  static std::size_t write(Protocol& protocol, Type const& in) { \
    DVLOG(3) << "write primitive " << #Class << ": "<< #Type; \
    return protocol.write##Method(in); \
  }

// stamp out specializations for primitive types
// TODO: Perhaps change ttype_value to a static constexpr member function, as
// those might instantiate faster than a constexpr objects
#define REGISTER_OVERLOAD(Class, Type, Method, TTypeValue) \
  template <> struct protocol_methods<type_class::Class, Type> { \
    REGISTER_OVERLOAD_COMMON(Class, Type, Method, TTypeValue) \
    template <bool, typename Protocol> \
    static std::size_t serialized_size(Protocol& protocol, Type const& in) { \
      return protocol.serializedSize##Method(in); \
    } \
  }

#define REGISTER_INTEGRAL(...) REGISTER_OVERLOAD(integral, __VA_ARGS__)
REGISTER_INTEGRAL(bool,         Bool,   T_BOOL);
REGISTER_INTEGRAL(std::int8_t,  Byte,   T_BYTE);
REGISTER_INTEGRAL(std::int16_t, I16,    T_I16);
REGISTER_INTEGRAL(std::int32_t, I32,    T_I32);
REGISTER_INTEGRAL(std::int64_t, I64,    T_I64);
#undef REGISTER_INTEGRAL

#define REGISTER_FP(...) REGISTER_OVERLOAD(floating_point, __VA_ARGS__)
REGISTER_FP(double,       Double, T_DOUBLE);
REGISTER_FP(float,        Float,  T_FLOAT);
#undef REGISTER_FP

// TODO: might not need to specialize on std::string, but rather
// just the string typeclass
REGISTER_OVERLOAD(string, std::string, String, T_STRING);

#undef REGISTER_OVERLOAD

#define REGISTER_OVERLOAD_ZC(Class, Type, Method, TTypeValue) \
  template <> struct protocol_methods<type_class::Class, Type> { \
    REGISTER_OVERLOAD_COMMON(Class, Type, Method, TTypeValue) \
    template <bool ZeroCopy, typename Protocol> \
    static typename std::enable_if<ZeroCopy, std::size_t>::type \
    serialized_size(Protocol& protocol, Type const& in) { \
      return protocol.serializedSizeZC##Method(in); \
    } \
    template <bool ZeroCopy, typename Protocol> \
    static typename std::enable_if<!ZeroCopy, std::size_t>::type \
    serialized_size(Protocol& protocol, Type const& in) { \
      return protocol.serializedSize##Method(in); \
    } \
  };

#define REGISTER_BINARY(...) REGISTER_OVERLOAD_ZC(binary, __VA_ARGS__)
REGISTER_BINARY(std::string,                   Binary, T_STRING);
REGISTER_BINARY(folly::IOBuf,                  Binary, T_STRING);
REGISTER_BINARY(std::unique_ptr<folly::IOBuf>, Binary, T_STRING);

#undef REGISTER_BINARY
#undef REGISTER_OVERLOAD_ZC
#undef REGISTER_OVERLOAD_COMMON

namespace detail {

// is_smart_pointer is a helper for determining if a type is a supported
// pointer type for cpp2.ref fields, while discrimiminating against the
// pointer corner case in Thrift (e.g., a unqiue_pointer<folly::IOBuf>)
template <typename>
struct is_smart_pointer : std::false_type {};
template <typename D>
struct is_smart_pointer<std::unique_ptr<folly::IOBuf, D>>
  : std::false_type {};

// supported smart pointer types for cpp2.ref_type fields
template <typename T, typename D>
struct is_smart_pointer<std::unique_ptr<T, D>> : std::true_type {};
template <typename T>
struct is_smart_pointer<std::shared_ptr<T>> : std::true_type {};

template <typename T>
using enable_if_smart_pointer =
  typename std::enable_if<is_smart_pointer<T>::value>::type;

template <typename T>
using disable_if_smart_pointer =
  typename std::enable_if<!is_smart_pointer<T>::value>::type;

} // namespace detail {

// handle dereferencing smart pointers
template <
  typename TypeClass,
  typename PtrType
>
struct protocol_methods <
  TypeClass,
  PtrType,
  typename detail::enable_if_smart_pointer<PtrType>
>
{
  using value_type   = typename PtrType::element_type;
  using type_methods = protocol_methods<TypeClass, value_type>;

  constexpr static protocol::TType ttype_value = type_methods::ttype_value;

  template <typename Protocol>
  static std::size_t read(Protocol& protocol, PtrType& out) {
    return type_methods::read(protocol, *out);
  }

  template <typename Protocol>
  static std::size_t write(Protocol& protocol, PtrType const& in) {
    return type_methods::write(protocol, *in);
  }

  template <bool ZeroCopy, typename Protocol>
  static std::size_t serialized_size(
    Protocol& protocol, PtrType const& in
  ) {
    return type_methods::template serialized_size<ZeroCopy>(protocol, *in);
  }
};

// Enumerations
template<typename Type>
struct protocol_methods<type_class::enumeration, Type> {
  constexpr static protocol::TType ttype_value = protocol::T_I32;

  using int_type = typename std::underlying_type<Type>::type;
  using int_methods = protocol_methods<type_class::integral, int_type>;

  template <typename Protocol>
  static std::size_t read(Protocol& protocol, Type& out) {
    int_type tmp;
    std::size_t xfer = int_methods::read(protocol, tmp);
    out = static_cast<Type>(tmp);
    return xfer;
  }

  template <typename Protocol>
  static std::size_t write(Protocol& protocol, Type const& in) {
    int_type tmp = static_cast<int_type>(in);
    return int_methods::template write<Protocol>(protocol, tmp);
  }

  template <bool ZeroCopy, typename Protocol>
  static std::size_t serialized_size(Protocol& protocol, Type const& in) {
    int_type tmp = static_cast<int_type>(in);
    return int_methods::template serialized_size<ZeroCopy>(protocol, tmp);
  }
};

// Lists
template<typename ElemClass, typename Type>
struct protocol_methods<type_class::list<ElemClass>, Type> {
  constexpr static protocol::TType ttype_value = protocol::T_LIST;

  using elem_type   = typename Type::value_type;
  using elem_tclass = ElemClass;
  static_assert(!std::is_same<elem_tclass, type_class::unknown>(),
    "Unable to serialize unknown list element");

  using elem_methods = protocol_methods<elem_tclass, elem_type>;

  template <typename Protocol>
  static std::size_t read(Protocol& protocol, Type& out) {
    std::size_t xfer = 0;
    std::uint32_t list_size = -1;
    TType reported_type = protocol::T_STOP;

    out = Type();

    xfer += protocol.readListBegin(reported_type, list_size);

    Type list;
    if(detail::is_unknown_container_size(list_size)) {
      // list size unknown, SimpleJSON protocol won't know type, either
      // so let's just hope that it spits out something that makes sense
      // (if it did set reported_type to something known)
      if(reported_type != protocol::T_STOP) {
        assert(reported_type == elem_methods::ttype_value);
      }

      for(decltype(list_size) i = 0; protocol.peekList(); i++) {
        // TODO: this should grow better (e.g., 1.5x each time)
        // For now, we just bump the container size by 1 each time
        // because that's what the currently generated Thrift code does.
        out.resize(i+1);
        xfer += elem_methods::read(protocol, out[i]);
      }
    } else {
      assert(reported_type == elem_methods::ttype_value);
      out.resize(list_size);
      for(decltype(list_size) i = 0; i < list_size; i++) {
        xfer += elem_methods::read(protocol, out[i]);
      }
    }

    xfer += protocol.readListEnd();
    return xfer;
  }

  template <typename Protocol>
  static std::size_t write(Protocol& protocol, Type const& out) {
    std::size_t xfer = 0;
    xfer += protocol.writeListBegin(
      elem_methods::ttype_value,
      out.size()
    );

    for(auto const& elem : out) {
      xfer += elem_methods::write(protocol, elem);
    }

    xfer += protocol.writeListEnd();
    return xfer;
  }

  template <bool ZeroCopy, typename Protocol>
  static std::size_t serialized_size(Protocol& protocol, Type const& out) {
    std::size_t xfer = 0;
    xfer += protocol.serializedSizeListBegin(
      elem_methods::ttype_value,
      out.size()
    );

    for(auto const& elem : out) {
      xfer += elem_methods::template serialized_size<ZeroCopy>(protocol, elem);
    }

    xfer += protocol.serializedSizeListEnd();
    return xfer;
  }
};

// Sets
template <typename ElemClass, typename Type>
struct protocol_methods<type_class::set<ElemClass>, Type> {
  constexpr static protocol::TType ttype_value = protocol::T_SET;

  // TODO: fair amount of shared code bewteen this and specialization for
  // type_class::list
  using elem_type   = typename Type::value_type;
  using elem_tclass = ElemClass;
  static_assert(!std::is_same<elem_tclass, type_class::unknown>(),
    "Unable to serialize unknown type");
  using elem_methods = protocol_methods<elem_tclass, elem_type>;

private:
  template <typename Protocol>
  static std::size_t consume_elem(Protocol& protocol, Type& out) {
    elem_type tmp;
    std::size_t xfer =
      elem_methods::read(protocol, tmp);
    out.insert(std::move(tmp));
    return xfer;
  }

public:
  template <typename Protocol>
  static std::size_t read(Protocol& protocol, Type& out) {
    std::size_t xfer = 0;
    // TODO: readSetBegin takes (TType, std::uint32_t)
    // instead of a (TType, std::size_t)
    std::uint32_t set_size = -1;
    TType reported_type = protocol::T_STOP;

    out = Type();
    xfer += protocol.readSetBegin(reported_type, set_size);
    if(detail::is_unknown_container_size(set_size)) {
      while(protocol.peekSet()) {
        xfer += consume_elem(protocol, out);
      }
    } else {
      assert(reported_type == elem_methods::ttype_value);
      for(decltype(set_size) i = 0; i < set_size; i++) {
        xfer += consume_elem(protocol, out);
      }
    }

    xfer += protocol.readSetEnd();
    return xfer;
  }

  template <typename Protocol>
  static std::size_t write(Protocol& protocol, Type const& in) {
    std::size_t xfer = 0;
    xfer += protocol.writeSetBegin(elem_methods::ttype_value, in.size());
    for(auto const& elem : in) {
      xfer += elem_methods::write(protocol, elem);
    }
    xfer += protocol.writeSetEnd();
    return xfer;
  }

  template <bool ZeroCopy, typename Protocol>
  static std::size_t serialized_size(Protocol& protocol, Type const& out) {
    std::size_t xfer = 0;
    xfer += protocol.serializedSizeSetBegin(
      elem_methods::ttype_value,
      out.size()
    );

    for(auto const& elem : out) {
      xfer += elem_methods::template serialized_size<ZeroCopy>(protocol, elem);
    }

    xfer += protocol.serializedSizeSetEnd();
    return xfer;
  }
};

// Maps
template <typename KeyClass, typename MappedClass, typename Type>
struct protocol_methods<type_class::map<KeyClass, MappedClass>, Type> {
  constexpr static protocol::TType ttype_value = protocol::T_MAP;

  using key_type    = typename Type::key_type;
  using key_tclass  = KeyClass;

  using mapped_type   = typename Type::mapped_type;
  using mapped_tclass = MappedClass;

  static_assert(!std::is_same<key_tclass, type_class::unknown>(),
    "Unable to serialize unknown key type in map");
  static_assert(!std::is_same<mapped_tclass, type_class::unknown>(),
    "Unable to serialize unknown mapped type in map");

    using key_methods    = protocol_methods<key_tclass, key_type>;
    using mapped_methods = protocol_methods<mapped_tclass, mapped_type>;

private:
  template <typename Protocol>
  static std::size_t consume_elem(Protocol& protocol, Type& out) {
    std::size_t xfer = 0;
    key_type key_tmp;
    xfer += key_methods::read(protocol, key_tmp);
    xfer += mapped_methods::read(protocol, out[std::move(key_tmp)]);
    return xfer;
  }

public:
  template <typename Protocol>
  static std::size_t read(Protocol& protocol, Type& out) {
    std::size_t xfer = 0;
    // TODO: see above re: readMapBegin taking a uint32_t size param
    std::uint32_t map_size = -1;
    TType rpt_key_type = protocol::T_STOP, rpt_mapped_type = protocol::T_STOP;
    out = Type();

    xfer += protocol.readMapBegin(rpt_key_type, rpt_mapped_type, map_size);
    DVLOG(3) << "read map begin: " << rpt_key_type << "/" << rpt_mapped_type
      << " (" << map_size << ")";

    if(detail::is_unknown_container_size(map_size)) {
      while(protocol.peekMap()) {
        xfer += consume_elem(protocol, out);
      }
    } else {
      // CompactProtocol does not transmit key/mapped types if
      // the map is empty
      if(map_size > 0) {
        assert(key_methods::ttype_value == rpt_key_type);
        assert(mapped_methods::ttype_value == rpt_mapped_type);
      }
      for(decltype(map_size) i = 0; i < map_size; i++) {
        xfer += consume_elem(protocol, out);
      }
    }

    xfer += protocol.readMapEnd();
    return xfer;
  }

  template <typename Protocol>
  static std::size_t write(Protocol& protocol, Type const& out) {
    std::size_t xfer = 0;
    DVLOG(3) << "start map write: " <<
      key_methods::ttype_value << "/" <<
      mapped_methods::ttype_value << " (" << out.size() << ")";
    xfer += protocol.writeMapBegin(
      key_methods::ttype_value,
      mapped_methods::ttype_value,
      out.size());

    for(auto const& elem_pair : out) {
      xfer += key_methods::write(protocol, elem_pair.first);
      xfer += mapped_methods::write(protocol, elem_pair.second);
    }

    xfer += protocol.writeMapEnd();
    return xfer;
  }

  template <bool ZeroCopy, typename Protocol>
  static std::size_t serialized_size(Protocol& protocol, Type const& out) {
    std::size_t xfer = 0;
    xfer += protocol.serializedSizeMapBegin(
      key_methods::ttype_value,
      mapped_methods::ttype_value,
      out.size()
    );

    for(auto const& elem_pair : out) {
      xfer += key_methods
        ::template serialized_size<ZeroCopy>(protocol, elem_pair.first);
      xfer += mapped_methods
        ::template serialized_size<ZeroCopy>(protocol, elem_pair.second);
    }

    xfer += protocol.serializedSizeMapEnd();
    return xfer;
  }
};

namespace detail {

// helper predicate for determining if a struct's MemberInfo is required
// to be read out of the protocol
template <typename MemberInfo>
struct is_required_field :
  std::integral_constant<
    bool,
    MemberInfo::optional::value == optionality::required
  > {};

// marks isset either on the required field array,
// or the appropriate member within the Struct being read
template <
  optionality opt,
  typename,
  typename MemberInfo,
  typename isset_array,
  typename Struct
>
typename std::enable_if<opt != optionality::required>::type
mark_isset(isset_array& /*isset*/, Struct& obj) {
  MemberInfo::mark_set(obj);
}

template <
  optionality opt,
  typename required_fields,
  typename MemberInfo,
  typename isset_array,
  typename Struct
>
typename std::enable_if<opt == optionality::required>::type
mark_isset(isset_array& isset, Struct& /*obj*/) {
  using fid_idx = fatal::index_of<required_fields, MemberInfo>;
  static_assert(fid_idx::value != fatal::size<required_fields>::value,
    "internal error: didn't find reqired field");
  assert(fid_idx::value < fatal::size<required_fields>::value);
  isset[fid_idx::value] = true;
}

template <typename T>
using extract_descriptor_fid = typename T::metadata::id;

template <typename T, typename Enable = void>
struct deref;

// General case: methods on deref are no-op, returning their input
template <typename T>
struct deref <
  T,
  disable_if_smart_pointer<T>
> {
    static T& clear_and_get(T& in) { return in; }
    static T const& get_const(T const& in) { return in; }
};

// Special case: We specifically *do not* dereference a unique pointer to
// an IOBuf, because this is a type that the protocol can (de)serialize
// directly
template <>
struct deref<std::unique_ptr<folly::IOBuf>> {
  using T =  std::unique_ptr<folly::IOBuf>;
  static T& clear_and_get(T& in) { return in; }
  static T const& get_const(T const& in) { return in; }
};

// General case: deref returns a reference to what the
// unique pointer contains
template <typename PtrType>
struct deref <
  PtrType,
  enable_if_smart_pointer<PtrType>
> {
  using T = typename PtrType::element_type;
  static T& clear_and_get(PtrType& in) {
    clear(in);
    return *in;
  }
  static T const& get_const(PtrType const& in) {
    return *in;
  }

private:
  static void clear(std::shared_ptr<T>& in) {
    in = std::make_shared<T>();
  }
  static void clear(std::unique_ptr<T>& in) {
    in = std::make_unique<T>();
  }
};

} // namespace detail

// specialization for variants (Thrift unions)
template <typename Union>
struct protocol_methods<type_class::variant, Union> {
  constexpr static protocol::TType ttype_value = protocol::T_STRUCT; // overlaps

  // using traits = reflect_union<Union>;
  using traits = fatal::variant_traits<Union>;
  using enum_traits = fatal::enum_traits<typename traits::id>;
  using id_name_strs = typename enum_traits::names;

  // Field ID -> descriptor
  using fid_to_descriptor_map = fatal::as_map<
    typename traits::descriptors,
    detail::extract_descriptor_fid
  >;

  // Union.Type id -> descriptor
  using sorted_ids = fatal::sort<
    fatal::transform<typename traits::descriptors, fatal::get_type::id>
  >;
  using id_to_descriptor_map = fatal::as_map<
    typename traits::descriptors,
    fatal::get_type::id
  >;

  private:
    // Visitor for a fatal prefix tree of union field names,
    // for mapping member field `fname` to  field `fid` and `ftype`
  struct member_fname_to_fid {
    template <typename TString>
    void operator ()(
      fatal::tag<TString>,
      field_id_t& fid,
      protocol::TType& ftype)
    {
      using enum_value = fatal::map_get<
        typename enum_traits::name_to_value,
        TString
      >;
      using descriptor = fatal::map_get<
        typename traits::by_id::map,
        enum_value
      >;

      using field_type   = typename descriptor::type;
      using field_tclass = typename descriptor::metadata::type_class;

      static_assert(!std::is_same<field_tclass, type_class::unknown>(),
        "Instantiation failure, unknown field type");
      static_assert(
        std::is_same<typename descriptor::metadata::name, TString>(),
        "Instantiation failure, descriptor name mismatch");

      DVLOG(3) << "(union) matched string: " << fatal::z_data<TString>()
        << ", fid: " << fid
        << ", ftype: " << ftype;

      fid   = descriptor::metadata::id::value;
      ftype = protocol_methods<field_tclass, field_type>::ttype_value;
    }
  };

  // Visitor for a fatal binary search on (sorted) field IDs,
  // sets the field `Fid` on the Union `obj` when called
  template <typename Protocol>
  struct set_member_by_fid {
    // Fid is a std::integral_constant<field_id_t, fid>
    template <typename Fid, std::size_t Index>
    void operator ()(
      fatal::indexed<Fid, Index>,
      const TType ftype,
      std::size_t& xfer,
      Protocol& protocol,
      Union& obj
    ) const {
      using descriptor = fatal::map_get<fid_to_descriptor_map, Fid>;
      constexpr field_id_t fid = Fid::value;

      using field_methods = protocol_methods<
          typename descriptor::metadata::type_class,
          typename descriptor::type>;

      if(ftype == field_methods::ttype_value) {
        typename descriptor::type tmp;
        typename descriptor::setter field_setter;
        xfer += field_methods::read(protocol, tmp);
        field_setter(obj, std::move(tmp));
      } else {
        xfer += protocol.skip(ftype);
      }
    }
  };

public:
  template <typename Protocol>
  static std::size_t read(Protocol& protocol, Union& out) {
    field_id_t fid = -1;
    std::size_t xfer = 0;
    protocol::TType ftype = protocol::T_STOP;
    std::string fname;

    xfer += protocol.readStructBegin(fname);

    DVLOG(3) << "began reading union: " << fname;
    xfer += protocol.readFieldBegin(fname, ftype, fid);
    if(ftype == protocol::T_STOP) {
      out.__clear();
    } else {
      // fid might not be known, such as in the case of the SimpleJSON protocol
      if(fid == std::numeric_limits<int16_t>::min()) {
        // if so, look up fid via fname
        assert(fname != "");
        auto found_ = fatal::prefix_tree<id_name_strs>::find(
          fname.begin(), fname.end(), member_fname_to_fid(), fid, ftype
        );
        assert(found_ == 1);
      }

      using sorted_fids = fatal::sort<
        fatal::transform<
          typename traits::descriptors,
          detail::extract_descriptor_fid
        >
      >;
      if(!fatal::sorted_search<sorted_fids>(
        fid, set_member_by_fid<Protocol>(), ftype, xfer, protocol, out))
      {
        DVLOG(3) << "didn't find field, fid: " << fid;
        xfer += protocol.skip(ftype);
      }

      xfer += protocol.readFieldEnd();
      xfer += protocol.readFieldBegin(fname, ftype, fid);
      if(UNLIKELY(ftype != protocol::T_STOP)) {
        // TODO: TProtocolException::throwUnionMissingStop() once
        // union deserialization is fixed
        xfer += protocol.readFieldEnd();
      }
      xfer += protocol.readStructEnd();
    }
    return xfer;
  }

private:
  struct write_member_by_fid {
    template <typename Id, std::size_t Index, typename Protocol>
    void operator ()(
      fatal::indexed<Id, Index>,
      std::size_t& xfer,
      Protocol& protocol,
      Union const& obj
    ) {
      using descriptor = fatal::map_get<id_to_descriptor_map, Id>;
      typename descriptor::getter getter;
      using methods = protocol_methods<
        typename descriptor::metadata::type_class,
        typename descriptor::type>;

      assert(Id::value == descriptor::id::value);

      DVLOG(3) << "writing union field "
        << fatal::z_data<typename descriptor::metadata::name>()
        << ", fid: " << descriptor::metadata::id::value
        << ", ttype: " << methods::ttype_value;

      xfer += protocol.writeFieldBegin(
        fatal::z_data<typename descriptor::metadata::name>(),
        methods::ttype_value,
        descriptor::metadata::id::value
      );
      auto const& tmp = getter(obj);
      using member_type = typename std::decay<decltype(tmp)>::type;
      xfer += methods::write(
        protocol,
        detail::deref<member_type>::get_const(tmp)
      );
      xfer += protocol.writeFieldEnd();
    }
  };

public:
  template <typename Protocol>
  static std::size_t write(Protocol& protocol, Union const& in) {
    std::size_t xfer = 0;
    DVLOG(3) << "begin writing union: "
      << fatal::z_data<typename traits::name>()
      << ", type: " << in.getType();
    xfer += protocol.writeStructBegin(fatal::z_data<typename traits::name>());
    fatal::sorted_search<sorted_ids>(
      in.getType(),
      write_member_by_fid(),
      xfer, protocol, in
    );
    xfer += protocol.writeFieldStop();
    xfer += protocol.writeStructEnd();
    DVLOG(3) << "end writing union";
    return xfer;
  }

private:
  template <bool ZeroCopy>
  struct size_member_by_type_id {
    template <typename Id, std::size_t Index, typename Protocol>
    void operator ()(
      fatal::indexed<Id, Index>,
      std::size_t& xfer,
      Protocol& protocol,
      Union const& obj
    )
    {
      using descriptor = fatal::map_get<id_to_descriptor_map, Id>;
      typename descriptor::getter getter;
      using methods = protocol_methods<
        typename descriptor::metadata::type_class,
        typename descriptor::type>;

      assert(Id::value == descriptor::id::value);

      DVLOG(3) << "sizing union field "
        << fatal::z_data<typename descriptor::metadata::name>()
        << ", fid: " << descriptor::metadata::id::value
        << ", ttype: " << methods::ttype_value;

      xfer += protocol.serializedFieldSize(
        fatal::z_data<typename descriptor::metadata::name>(),
        methods::ttype_value,
        descriptor::metadata::id::value
      );
      auto const& tmp = getter(obj);
      using member_type = typename std::decay<decltype(tmp)>::type;
      xfer += methods::template serialized_size<ZeroCopy>(
        protocol,
        detail::deref<member_type>::get_const(tmp)
      );
    }
  };

public:
  template <bool ZeroCopy, typename Protocol>
  static std::size_t serialized_size(Protocol& protocol, Union const& in) {
    std::size_t xfer = 0;

    xfer += protocol.serializedStructSize(
      fatal::z_data<typename traits::name>());
    fatal::sorted_search<sorted_ids>(
      in.getType(),
      size_member_by_type_id<ZeroCopy>(),
      xfer, protocol, in
    );
    xfer += protocol.serializedSizeStop();
    return xfer;
  }
};

// specialization for structs
template <typename Struct>
struct protocol_methods<type_class::structure, Struct> {
  constexpr static protocol::TType ttype_value = protocol::T_STRUCT;

private:
  using traits = apache::thrift::reflect_struct<Struct>;

  // fatal::type_list
  using member_names = fatal::map_keys<typename traits::members>;

  // fatal::type_list
  using all_fields = fatal::partition<
    fatal::map_values<typename traits::members>,
    fatal::applier<detail::is_required_field>
  >;
  using required_fields = fatal::first<all_fields>;
  using optional_fields = fatal::second<all_fields>;

  using isset_array = std::array<bool, fatal::size<required_fields>::value>;

  // mapping member fname -> fid
  struct member_fname_to_fid {
    template <typename TString>
    void operator ()(
      fatal::tag<TString>,
      field_id_t& fid,
      protocol::TType& ftype) {
      using member = fatal::map_get<typename traits::members, TString>;
      fid = member::id::value;
      ftype = protocol_methods<
        typename member::type_class,
        typename member::type
      >::ttype_value;

      DVLOG(3) << "matched string: " << fatal::z_data<TString>()
        << ", fid: " << fid
        << ", ftype: " << ftype;
    }
  };

  // match on member field id -> set that field
  struct set_member_by_fid {
    // Fid is a std::integral_constant<field_id_t, fid>
    template <
      typename Fid,
      std::size_t Index,
      typename Protocol,
      typename isset_array
    >
    void operator ()(
      fatal::indexed<Fid, Index>,
      const TType ftype,
      std::size_t& xfer,
      Protocol& protocol,
      Struct& obj,
      isset_array& required_isset)
    {
      constexpr field_id_t fid = Fid::value;
      // fatal::map {std::integral_constant<field_id_t, Id> => MemberInfo}
      using id_to_member_map = fatal::as_map<
        fatal::map_values<typename traits::members>,
        fatal::get_type::id
      >;
      using member = fatal::map_get<id_to_member_map, Fid>;
      using getter = typename member::getter;

      using protocol_method = protocol_methods<
        typename member::type_class,
        typename member::type
      >;

      using member_type = typename std::decay<decltype(getter::ref(obj))>::type;

      if(ftype == protocol_method::ttype_value) {
        detail::mark_isset<
            member::optional::value,
            required_fields,
            member
          >(required_isset, obj);
        xfer += protocol_method::read(
            protocol,
            detail::deref<member_type>::clear_and_get(getter::ref(obj))
          );
      } else {
        xfer += protocol.skip(ftype);
      }
    }
  };

public:
  template <typename Protocol>
  static std::size_t read(Protocol& protocol, Struct& out) {
    std::size_t xfer = 0;
    std::string fname;
    apache::thrift::protocol::TType ftype = protocol::T_STOP;
    std::int16_t fid = -1;
    isset_array required_isset = {};

    xfer += protocol.readStructBegin(fname);
    DVLOG(3) << "start reading struct: " << fname;

    while(true) {
      xfer += protocol.readFieldBegin(fname, ftype, fid);
      DVLOG(3) << "type: " << ftype
                 << ", fname: " << fname
                 << ", fid: " << fid;

      if(ftype == apache::thrift::protocol::T_STOP) {
        break;
      }

      // fid might not be known, such as in the case of SimpleJSON protocol
      if(fid == std::numeric_limits<int16_t>::min()) {
        // if so, look up fid via fname
        assert(fname != "");
        auto found_ = fatal::prefix_tree<member_names>::find(
          fname.begin(), fname.end(), member_fname_to_fid(), fid, ftype
        );
        assert(found_ == 1);
      }

      using sorted_fids  = fatal::sort<
        fatal::transform<
          fatal::map_values<typename traits::members>,
          fatal::get_type::id
        >
      >;
      if(!fatal::sorted_search<sorted_fids>(
          fid,
          set_member_by_fid(),
          ftype,
          xfer,
          protocol,
          out,
          required_isset
        )
      ) {
        DVLOG(3) << "didn't find field, fid: " << fid
                   << ", fname: " << fname;
        xfer += protocol.skip(ftype);
      }

      xfer += protocol.readFieldEnd();
    }

    for(bool matched : required_isset) {
      if(!matched) {
        throw apache::thrift::TProtocolException(
          TProtocolException::MISSING_REQUIRED_FIELD,
          "Required field was not found in serialized data!");
      }
    }

    xfer += protocol.readStructEnd();
    return xfer;
  }

private:
  template <
    typename Protocol,
    typename Member,
    typename TypeClass,
    typename MemberType,
    typename Methods,
    optionality optional,
    typename Enable = void
  >
  struct field_writer;

  // generic field writer
  template <
    typename Protocol,
    typename Member,
    typename TypeClass,
    typename MemberType,
    typename Methods,
    optionality opt
  >
  struct field_writer
  <
    Protocol, Member, TypeClass, MemberType, Methods, opt,
    detail::disable_if_smart_pointer<MemberType>
  > {
    static std::size_t write(Protocol& protocol, MemberType const& in) {
      std::size_t xfer = 0;
      // TODO: can maybe get rid of get_const?
      xfer += protocol.writeFieldBegin(
        fatal::z_data<typename Member::name>(),
        Methods::ttype_value,
        Member::id::value
      );
      xfer += Methods::write(
        protocol,
        detail::deref<MemberType>::get_const(in)
      );
      xfer += protocol.writeFieldEnd();
      return xfer;
    }
  };

  // writer for default/required ref structrs
  template <
    typename Protocol,
    typename Member,
    typename PtrType,
    typename Methods,
    optionality opt
  >
  struct field_writer <
    Protocol, Member, type_class::structure, PtrType, Methods, opt,
    detail::enable_if_smart_pointer<PtrType>
  > {
    using struct_type  = typename PtrType::element_type;

    static
    std::size_t write(Protocol& protocol, PtrType const& in) {
      std::size_t xfer = 0;
      // `in` is a pointer to a struct.
      // if not present, and this isn't an optional field,
      // write out an empty struct
      if(in) {
        xfer += field_writer<
          Protocol,
          Member,
          type_class::structure,
          struct_type,
          Methods,
          opt
        >::write(protocol, *in);
      }
      else {
        using field_traits = reflect_struct<struct_type>;
        DVLOG(3) << "empty ref struct, writing blank struct! "
          << fatal::z_data<typename field_traits::name>();
        xfer += protocol.writeFieldBegin(
          fatal::z_data<typename Member::name>(),
          Methods::ttype_value,
          Member::id::value
        );
        xfer += protocol.writeStructBegin(
          fatal::z_data<typename field_traits::name>());
        xfer += protocol.writeFieldStop();
        xfer += protocol.writeStructEnd();
        xfer += protocol.writeFieldEnd();
      }

      return xfer;
    }
  };

  // writer for optional ref structs
  template <
    typename Protocol,
    typename Member,
    typename PtrType,
    typename Methods
  >
  struct field_writer <
    Protocol, Member, type_class::structure, PtrType, Methods,
    optionality::optional,
    detail::enable_if_smart_pointer<PtrType>
  > {
    static std::size_t write(Protocol& protocol, PtrType const& in) {
      if(in) {
        return field_writer<
          Protocol,
          Member,
          type_class::structure,
          PtrType,
          Methods,
          optionality::required
        >::write(protocol, in);
      }
      else {
        return 0;
      }
    }
  };

  struct member_writer {
    template <typename Member, std::size_t Index, typename Protocol>
    void operator()(
      fatal::indexed<Member, Index>,
      Protocol& protocol,
      Struct const& in,
      std::size_t& xfer)
    {
      using methods = protocol_methods<
        typename Member::type_class,
        typename Member::type
      >;

      if(
        (Member::optional::value == optionality::required_of_writer) ||
        Member::is_set(in))
      {
        DVLOG(3) << "start field write: "
          << fatal::z_data<typename Member::name>()
          << " ttype:" << methods::ttype_value
          << ", id:" << Member::id::value;

        auto const& got = Member::getter::ref(in);
        using member_type = typename std::decay<decltype(got)>::type;

        xfer += field_writer<
          Protocol,
          Member,
          typename Member::type_class,
          member_type,
          methods,
          Member::optional::value>::write(protocol, got);
      }
    }
  };

  template <
    bool ZeroCopy,
    typename Protocol,
    typename Member,
    typename TypeClass,
    typename MemberType,
    typename Methods,
    optionality,
    typename Enable = void
  >
  struct field_size;

  // generic field size
  template <
    bool ZeroCopy,
    typename Protocol,
    typename Member,
    typename TypeClass,
    typename MemberType,
    typename Methods,
    optionality opt
  >
  struct field_size <
    ZeroCopy, Protocol, Member, TypeClass, MemberType, Methods, opt,
    detail::disable_if_smart_pointer<MemberType>
  > {
    static std::size_t size(Protocol& protocol, MemberType const& in) {
      std::size_t xfer = 0;
      xfer += protocol.serializedFieldSize(
        fatal::z_data<typename Member::name>(),
        Methods::ttype_value,
        Member::id::value
      );
      xfer += Methods::template serialized_size<ZeroCopy>(
        protocol,
        detail::deref<MemberType>::get_const(in)
      );
      return xfer;
    }
  };

  // size for default/required ref structrs
  template <
    bool ZeroCopy,
    typename Protocol,
    typename Member,
    typename PtrType,
    typename Methods,
    optionality opt
  >
  struct field_size <
    ZeroCopy, Protocol, Member, type_class::structure, PtrType, Methods, opt,
    detail::enable_if_smart_pointer<PtrType>
  > {
    using struct_type = typename PtrType::element_type;

    static
    std::size_t size(Protocol& protocol, PtrType const& in) {
      std::size_t xfer = 0;
      if(in) {
        xfer += field_size<
          ZeroCopy,
          Protocol,
          Member,
          type_class::structure,
          struct_type,
          Methods,
          opt
        >::size(protocol, *in);
      }
      else {
        using field_traits = reflect_struct<struct_type>;
        DVLOG(3) << "empty ref struct, sizing blank struct! "
          << fatal::z_data<typename field_traits::name>();
        xfer += protocol.serializedFieldSize(
          fatal::z_data<typename Member::name>(),
          Methods::ttype_value,
          Member::id::value
        );
        xfer += protocol.serializedStructSize(
          fatal::z_data<typename field_traits::name>());
        xfer += protocol.serializedSizeStop();
      }

      return xfer;
    }
  };

  // size for optional ref structs
  template <
    bool ZeroCopy,
    typename Protocol,
    typename Member,
    typename PtrType,
    typename Methods
  >
  struct field_size <
    ZeroCopy, Protocol, Member, type_class::structure, PtrType, Methods,
    optionality::optional,
    detail::enable_if_smart_pointer<PtrType>
  > {
    static std::size_t size(Protocol& protocol, PtrType const& in) {
      if(in) {
        return field_size<
          ZeroCopy,
          Protocol,
          Member,
          type_class::structure,
          PtrType,
          Methods,
          optionality::required
        >::size(protocol, in);
      }
      else {
        return 0;
      }
    }
  };

  template <bool ZeroCopy>
  struct member_size {
    template <typename Member, std::size_t Index, typename Protocol>
    void inline operator()(
      fatal::indexed<Member, Index>,
      Protocol& protocol,
      Struct const& in,
      std::size_t& xfer)
    {
      using methods = protocol_methods<
        typename Member::type_class,
        typename Member::type
      >;

      auto const& got = Member::getter::ref(in);
      using member_type = typename std::decay<decltype(got)>::type;

      xfer += field_size <
        ZeroCopy,
        Protocol,
        Member,
        typename Member::type_class,
        member_type,
        methods,
        Member::optional::value>::size(protocol, got);
    }
  };

public:
  template <typename Protocol>
  static std::size_t write(Protocol& protocol, Struct const& in) {
    std::size_t xfer = 0;

    xfer += protocol.writeStructBegin(fatal::z_data<typename traits::name>());
    fatal::foreach<fatal::map_values<typename traits::members>>(
      member_writer(), protocol, in, xfer
    );
    xfer += protocol.writeFieldStop();
    xfer += protocol.writeStructEnd();
    return xfer;
  }

  template <bool ZeroCopy, typename Protocol>
  static std::size_t serialized_size(Protocol& protocol, Struct const& in) {
    std::size_t xfer = 0;
    xfer += protocol.serializedStructSize(
      fatal::z_data<typename traits::name>());
    fatal::foreach<fatal::map_values<typename traits::members>>(
      member_size<ZeroCopy>(), protocol, in, xfer
    );
    xfer += protocol.serializedSizeStop();
    return xfer;
  }
};

/**
 * Entrypoints for using new serialization methods
 *
 * // C++
 * MyStruct a;
 * MyUnion b;
 * CompactProtocolReader reader;
 * CompactProtocolReader writer;
 *
 * serializer_read(a, reader);
 * serializer_write(b, writer);
 *
 * @author: Dylan Knutson <dymk@fb.com>
 */

template <typename Type, typename Protocol>
std::size_t serializer_read(Type& out, Protocol& protocol) {
  return protocol_methods<reflect_type_class<Type>, Type>::read(protocol, out);
}

template <typename Type, typename Protocol>
std::size_t serializer_write(Type const& in, Protocol& protocol) {
  return protocol_methods<reflect_type_class<Type>, Type>::write(protocol, in);
}

template <typename Type, typename Protocol>
std::size_t serializer_serialized_size(Type const& in, Protocol& protocol) {
  return protocol_methods<reflect_type_class<Type>, Type>
    ::template serialized_size<false>(protocol, in);
}

template <typename Type, typename Protocol>
std::size_t serializer_serialized_size_zc(Type const& in, Protocol& protocol) {
  return protocol_methods<reflect_type_class<Type>, Type>
    ::template serialized_size<true>(protocol, in);
}


} } // namespace apache::thrift
