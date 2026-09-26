#include "PapyrusOCBP.h"

#include "Game.h"
//#include "f4se/PapyrusVM.h"


#include <functional>
#include <algorithm>



#include "SimObj.h"

std::unordered_map<UInt32, std::unordered_map<std::string, bool>> boneIgnores;

namespace papyrusOCBP
{
	void SetBoneToggle(std::monostate, Actor* actor, bool toggle, BSFixedString boneName)
	{
		boneIgnores[actor->formID][std::string(boneName.c_str())] = toggle;
	}

	bool GetBoneToggle(std::monostate, Actor* actor, BSFixedString boneName)
	{
		if (boneIgnores.find(actor->formID) != boneIgnores.end()) {
			auto actorsBoneIgns = boneIgnores.at(actor->formID);
			if (actorsBoneIgns.find(std::string(boneName.c_str())) != actorsBoneIgns.end()) {
				return actorsBoneIgns.at(std::string(boneName.c_str()));
			}
		}

		return false;
	}

	void ClearBoneToggles(std::monostate)
	{
		boneIgnores.clear();
	}

}

void papyrusOCBP::RegisterFuncs(RE::BSScript::IVirtualMachine* vm)
{
	vm->BindNativeMethod("OCBP_API"sv, "SetBoneToggle"sv, papyrusOCBP::SetBoneToggle, true);
	vm->BindNativeMethod("OCBP_API"sv, "GetBoneToggle"sv, papyrusOCBP::GetBoneToggle, true);
	vm->BindNativeMethod("OCBP_API"sv, "ClearBoneToggles"sv, papyrusOCBP::ClearBoneToggles, true);
}