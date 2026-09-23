#include "BottomBarHint.h"

#include "Localization.h"
#include "MenuAssign.h"
#include "Settings.h"

#include <string>
#include <utility>

namespace HKS::BottomBarHint
{
	namespace
	{
		// How to reach one menu's hint row, and what to fix up once we have widened it.
		struct Target
		{
			const char* menuPath;     // GFx path to the menu object owning the method
			const char* method;       // the method that rebuilds the row
			const char* container;    // member holding the row (hidden when there is nothing to hint)
			const char* panel;        // member of the container that is the ButtonPanel; null = the container
			bool        selectedArg;  // the method's first argument says whether a row is highlighted
			bool        recenter;     // the row centres itself on its own width, so redo that
		};

		constexpr Target kItemMenu{
			"_root.Menu_mc", "updateBottomBar", "navPanel", nullptr, true, false
		};
		// Untarnished UI draws its favorites hints in two centred rows of six buttons; the
		// per-item ones live in row1, which uses two of them.
		constexpr Target kFavorites{
			"_root.MenuHolder.Menu_mc", "updateNavButtons", "navPanel", "row1", false, true
		};

		// The button data is { text: <label>, controls: <control descriptor> }. The
		// descriptor may name a game control or, as here, carry a raw DX scancode -- the
		// same shape skyui.defines.Input uses for Shift/Tab/Enter, and the one Untarnished
		// already uses for its own group keys. So the panel draws the matching keycap for
		// us, and it follows a remapped modifier by itself.
		bool AddHint(RE::GFxMovie* a_movie, RE::GFxValue& a_panel, const std::string& a_text,
			std::uint32_t a_scancode)
		{
			RE::GFxValue data;
			RE::GFxValue controls;
			RE::GFxValue text;
			a_movie->CreateObject(&data);
			a_movie->CreateObject(&controls);
			a_movie->CreateString(&text, a_text.c_str());
			if (!data.IsObject() || !controls.IsObject()) {
				return false;
			}
			controls.SetMember("keyCode", RE::GFxValue{ static_cast<double>(a_scancode) });
			data.SetMember("text", text);
			data.SetMember("controls", controls);

			RE::GFxValue added;
			if (!a_panel.Invoke("addButton", &added, &data, 1)) {
				return false;
			}
			// addButton returns undefined when the panel has no free button left.
			return added.IsObject();
		}

		bool IsHidden(RE::GFxValue& a_obj)
		{
			RE::GFxValue visible;
			return a_obj.GetMember("_visible", &visible) && visible.IsBool() && !visible.GetBool();
		}

		class UpdateHintsHook : public RE::GFxFunctionHandler
		{
		public:
			UpdateHintsHook(RE::GFxValue a_old, const Target& a_target) :
				_old(std::move(a_old)),
				_target(a_target)
			{}

			void Call(Params& a_params) override
			{
				_old.Invoke("call", a_params.retVal, a_params.argsWithThisRef, a_params.argCount + 1);

				if (!Settings::ShowAssignHint() || !a_params.thisPtr || !a_params.movie) {
					return;
				}
				// With nothing highlighted the row shows the menu-wide actions (Exit,
				// Search, ...), where an item hint makes no sense.
				if (_target.selectedArg &&
					(a_params.argCount < 1 || !a_params.args[0].IsBool() || !a_params.args[0].GetBool())) {
					return;
				}
				// And only for a row that can actually be bound -- which also rules out the
				// Active Effects tab, whose rows carry a spell's formId but equip nothing.
				if (!MenuAssign::GetSelectedAssignTarget()) {
					return;
				}

				RE::GFxValue container;
				if (!a_params.thisPtr->GetMember(_target.container, &container) || !container.IsObject() ||
					IsHidden(container)) {
					return;
				}
				RE::GFxValue panel = container;
				if (_target.panel && (!container.GetMember(_target.panel, &panel) || !panel.IsObject())) {
					return;
				}

				// No hint for a modifier the menu uses itself -- it does nothing of ours there.
				const auto assignKey = MenuAssign::UsableModifier(Settings::AssignModifier());
				const auto groupKey = MenuAssign::UsableModifier(Settings::GroupModifier());
				bool       added = false;
				if (assignKey != 0) {
					added = AddHint(a_params.movie, panel, Localization::Get("$STB_HK_Hint_Assign"), assignKey);
				}
				if (groupKey != 0) {
					added = AddHint(a_params.movie, panel, Localization::Get("$STB_HK_Hint_Group"), groupKey) || added;
				}
				if (!added) {
					return;
				}

				// The panel only lays buttons out on request; `true` means do it now rather
				// than on the next interval, so the row does not visibly reflow.
				RE::GFxValue instant{ true };
				panel.Invoke("updateButtons", nullptr, &instant, 1);

				// A row the menu centres on its own width was centred before we widened it,
				// so re-apply the menu's own formula instead of leaving it off to one side.
				if (_target.recenter) {
					RE::GFxValue width;
					if (panel.GetMember("_width", &width) && width.IsNumber()) {
						panel.SetMember("_x", RE::GFxValue{ -width.GetNumber() / 2.0 });
					}
				}
			}

		private:
			RE::GFxValue  _old;
			const Target& _target;
		};

		void Install(RE::IMenu* a_menu, const Target& a_target)
		{
			if (!a_menu || !a_menu->uiMovie) {
				return;
			}

			// Wrapped on the menu OBJECT, not on a class prototype: every SkyUI item menu
			// has its own class but they all sit at the same path, and an own property
			// shadows the inherited method for calls made on this instance. One path covers
			// them all, and there is nothing to undo when the menu closes.
			RE::GFxValue menuObj;
			if (!a_menu->uiMovie->GetVariable(&menuObj, a_target.menuPath) || !menuObj.IsObject()) {
				return;
			}

			RE::GFxValue oldMethod;
			if (!menuObj.GetMember(a_target.method, &oldMethod) || !oldMethod.IsObject()) {
				logger::info("BottomBarHint: {} has no {} -- no hints on this menu",
					a_target.menuPath, a_target.method);
				return;
			}

			auto         impl = RE::make_gptr<UpdateHintsHook>(std::move(oldMethod), a_target);
			RE::GFxValue newMethod;
			a_menu->uiMovie->CreateFunction(&newMethod, impl.get());
			menuObj.SetMember(a_target.method, newMethod);
			logger::info("BottomBarHint: hooked {}", a_target.method);
		}
	}

	void SetupItemMenu(RE::IMenu* a_menu)
	{
		Install(a_menu, kItemMenu);
	}

	void SetupFavorites(RE::IMenu* a_menu)
	{
		Install(a_menu, kFavorites);
	}
}
