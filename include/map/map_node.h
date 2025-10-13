#ifndef MAP_NODE_H
#define MAP_NODE_H

#include "godot_cpp/core/method_ptrcall.hpp"
#include "godot_cpp/variant/string.hpp"
#include <cstdint>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;

class MapNode : public Resource {
  GDCLASS(MapNode, Resource);

public:
  enum class Type : uint8_t {
    NotAssigned = 0,
    Enemy = 1,
    Loot = 2,
    Shelter = 3,
    Wenny = 4,
    Boss
  };

  [[nodiscard]] static constexpr const char *type_to_string(Type t) noexcept {
    using enum Type;
    switch (t) {
    case NotAssigned:
      return "NOT_ASSIGNED";
    case Enemy:
      return "ENEMY";
    case Loot:
      return "LOOT";
    case Shelter:
      return "SHELTER";
    case Wenny:
      return "WENNY";
    case Boss:
      return "BOSS";
    }
    return "UNKNOWN";
  }

  [[nodiscard]] static constexpr char type_to_char(Type t) noexcept {
    return type_to_string(t)[0];
  }

  // For godot debugging
  String _to_string() const;

private:
  Type type = Type::NotAssigned;
  int row = 0;
  int column = 0;
  Vector2 position{};
  TypedArray<MapNode> next_nodes;
  bool selected = false;

protected:
  static void _bind_methods();

public:
  // Setters
  void set_type(Type p_type) noexcept { type = p_type; }
  void set_row(int p_row) noexcept { row = p_row; }
  void set_column(int p_column) noexcept { column = p_column; }
  void set_position(const Vector2 &p_position) noexcept {
    position = p_position;
  }
  void set_next_nodes(const TypedArray<MapNode> &p_next_nodes) {
    next_nodes = p_next_nodes;
  }
  void set_selected(bool p_selected) noexcept { selected = p_selected; }

  // Getters
  [[nodiscard]] Type get_type() const noexcept { return type; }
  [[nodiscard]] int get_row() const noexcept { return row; }
  [[nodiscard]] int get_column() const noexcept { return column; }
  [[nodiscard]] Vector2 get_position() const noexcept { return position; }
  [[nodiscard]] TypedArray<MapNode> get_next_nodes() const {
    return next_nodes;
  }
  [[nodiscard]] bool is_selected() const noexcept { return selected; }

  // returns Type as int for Variant compatibility with godot
  [[nodiscard]] int get_type_value() const noexcept {
    return static_cast<int>(type);
  }

  // Utility Methods
  [[nodiscard]] bool has_next_nodes() const noexcept {
    return next_nodes.size() > 0;
  }

  void add_next_node(const Ref<MapNode> &node) {
    if (node.is_valid()) {
      next_nodes.append(node);
    }
  }

  void clear_next_nodes() noexcept { next_nodes.clear(); }
};
// Tell Godot how to handle MapNode::Type as a type
namespace godot {
template <> struct GetTypeInfo<MapNode::Type> {
  static constexpr GDExtensionVariantType VARIANT_TYPE =
      GDEXTENSION_VARIANT_TYPE_INT;
  static constexpr GDExtensionClassMethodArgumentMetadata METADATA =
      GDEXTENSION_METHOD_ARGUMENT_METADATA_INT_IS_INT32;
  static inline PropertyInfo get_class_info() {
    return PropertyInfo(Variant::INT, "Type");
  }
};

template <> struct VariantCaster<MapNode::Type> {
  static _FORCE_INLINE_ MapNode::Type cast(const Variant &p_variant) {
    return static_cast<MapNode::Type>(static_cast<int64_t>(p_variant));
  }
};

template <> struct PtrToArg<MapNode::Type> {
  _FORCE_INLINE_ static MapNode::Type convert(const void *p_ptr) {
    return static_cast<MapNode::Type>(
        *reinterpret_cast<const int64_t *>(p_ptr));
  }
  typedef int64_t EncodeT;
  _FORCE_INLINE_ static void encode(MapNode::Type p_val, void *p_ptr) {
    *reinterpret_cast<int64_t *>(p_ptr) = static_cast<int64_t>(p_val);
  }
};

} // namespace godot

#endif // MAP_NODE_H
