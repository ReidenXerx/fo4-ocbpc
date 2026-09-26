#pragma once
// fo4-anatomy: the Runtime Database build's view of the game. The classic code reads the game through the F4SE 0.6.23
// SDK (actor->unkF0->rootNode, obj->m_localTransform, BSSkin::Instance::bones ...); here the same reads go through
// CommonLibF4RD, with every member that CommonLibF4RD does not define measured ONCE, in this file, so the layout guard
// (Layout.cpp) checks exactly these on the running executable and turns a feature off if one differs.
#include "NiMath.h"

// the F4SE SDK's integer names, used all through the classic code
using UInt8 = std::uint8_t;
using UInt16 = std::uint16_t;
using UInt32 = std::uint32_t;
using UInt64 = std::uint64_t;
using SInt8 = std::int8_t;
using SInt16 = std::int16_t;
using SInt32 = std::int32_t;
using SInt64 = std::int64_t;

using RE::Actor;
using RE::BSFixedString;
using RE::NiAVObject;
using RE::NiNode;
using RE::TESForm;
using RE::TESNPC;
using RE::TESObjectREFR;
using RE::TESRace;
using RE::BSGeometry;

// the F4SE SDK's transform types sit on CommonLib's node memory unchanged
static_assert(offsetof(RE::NiAVObject, parent) == 0x28);
static_assert(offsetof(RE::NiAVObject, local) == 0x30);
static_assert(offsetof(RE::NiAVObject, world) == 0x70);
static_assert(sizeof(RE::NiTransform) == sizeof(NiTransform));

namespace G
{
	// ---- nodes (CommonLibF4RD's NiAVObject / NiNode)
	inline NiTransform& Local(NiAVObject* a_obj) { return *reinterpret_cast<NiTransform*>(&a_obj->local); }
	inline NiTransform& World(NiAVObject* a_obj) { return *reinterpret_cast<NiTransform*>(&a_obj->world); }
	inline const NiTransform& World(const NiAVObject* a_obj) { return *reinterpret_cast<const NiTransform*>(&a_obj->world); }
	inline NiNode* Parent(NiAVObject* a_obj) { return a_obj->parent; }
	inline const char* Name(const NiAVObject* a_obj) { return a_obj->name.c_str(); }
	inline NiNode* AsNode(NiAVObject* a_obj) { return a_obj ? a_obj->IsNode() : nullptr; }

	inline NiAVObject* Find(NiAVObject* a_under, const char* a_name)
	{
		if (!a_under) {
			return nullptr;
		}
		BSFixedString name(a_name);
		return a_under->GetObjectByName(name);
	}

	// a node's children, as the classic code walked them (m_children.m_data[i] for i < m_emptyRunStart)
	inline std::uint32_t ChildCount(const NiNode* a_node) { return a_node->children.size(); }
	inline NiAVObject* Child(NiNode* a_node, std::uint32_t a_i) { return a_node->children[a_i].get(); }

	// ---- references and actors
	// the classic actor->unkF0->rootNode: the loaded 3D (LOADED_REF_DATA::data3D, +0x08 of TESObjectREFR +0xF0)
	inline NiAVObject* Root(const TESObjectREFR* a_ref)
	{
		return a_ref && a_ref->loadedData ? a_ref->loadedData->data3D.get() : nullptr;
	}
	inline Actor* LookupActor(std::uint32_t a_formID)
	{
		auto* form = a_formID ? TESForm::GetFormByID(a_formID) : nullptr;
		return form ? form->As<Actor>() : nullptr;
	}
	inline std::string RefName(TESObjectREFR* a_ref)
	{
		auto* base = a_ref ? a_ref->GetObjectReference() : nullptr;
		return base ? std::string{ RE::TESFullName::GetFullName(*base) } : std::string{};
	}
	// the classic code kept the SDK's const char* (GetReferenceName): a buffer per thread, valid until the next call
	inline const char* RefNameC(TESObjectREFR* a_ref)
	{
		thread_local std::string buffer;
		buffer = RefName(a_ref);
		return buffer.c_str();
	}
	inline bool NameIs(const NiAVObject* a_obj, const char* a_name) { return a_obj && std::strcmp(a_obj->name.c_str(), a_name) == 0; }
	inline bool Deleted(const TESForm* a_form) { return (a_form->formFlags & (1u << 5)) != 0; }   // kDeleted, TESForm +0x10

	// ---- what CommonLibF4RD does not define: measured on 1.10.163 (the classic F4SE SDK's headers), checked on the
	// running executable by the layout guard before any feature that reads them is switched on
	namespace Measured
	{
		constexpr std::size_t kGeometryShaderProperty = 0x138;   // BSGeometry::shaderProperty (NiPointer)
		constexpr std::size_t kGeometrySkinInstance = 0x140;     // BSGeometry::skinInstance (NiPointer<BSSkin::Instance>)
		constexpr std::size_t kSkinBones = 0x10;                 // BSSkin::Instance::bones (tArray<NiNode*>)
		constexpr std::size_t kSkinWorldTransforms = 0x28;       // BSSkin::Instance::worldTransforms (tArray<NiTransform*>)
		constexpr std::size_t kShaderLastRenderPass = 0x2C;      // BSShaderProperty::iLastRenderPassState
		constexpr std::size_t kShaderMaterial = 0x58;            // BSShaderProperty::shaderMaterial
	}

	// the F4SE SDK's tArray: entries, capacity, count
	template <class T>
	struct Array
	{
		T* entries;
		std::uint32_t capacity;
		std::uint32_t pad0C;
		std::uint32_t count;
		std::uint32_t pad14;
	};
	static_assert(sizeof(Array<void*>) == 0x18);

	struct SkinInstance;   // BSSkin::Instance, by the offsets above

	inline RE::BSGeometry* AsGeometry(NiAVObject* a_obj) { return a_obj ? a_obj->IsGeometry() : nullptr; }
	inline SkinInstance* Skin(RE::BSGeometry* a_geo)
	{
		return *reinterpret_cast<SkinInstance**>(reinterpret_cast<std::uintptr_t>(a_geo) + Measured::kGeometrySkinInstance);
	}
	inline Array<NiNode*>& SkinBones(SkinInstance* a_skin)
	{
		return *reinterpret_cast<Array<NiNode*>*>(reinterpret_cast<std::uintptr_t>(a_skin) + Measured::kSkinBones);
	}
	inline Array<NiTransform*>& SkinWorldTransforms(SkinInstance* a_skin)
	{
		return *reinterpret_cast<Array<NiTransform*>*>(reinterpret_cast<std::uintptr_t>(a_skin) + Measured::kSkinWorldTransforms);
	}
}
