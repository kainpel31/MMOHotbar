#pragma once

#include <functional>
#include <string>
#include <vector>

// Adapted from C:\dev\STB\include\MessageBox.h (SkyrimScripting::ShowMessageBox).
// A vanilla message box with arbitrary buttons; the callback receives the index
// of the pressed button.
namespace HKS
{
	inline void ShowMessageBox(
		const std::string&                bodyText,
		std::function<void(unsigned int)> callback,
		std::vector<std::string>          buttons = { "Ok" })
	{
		auto* factoryManager = RE::MessageDataFactoryManager::GetSingleton();
		if (!factoryManager) {
			return;
		}

		auto* uiStringHolder = RE::InterfaceStrings::GetSingleton();
		if (!uiStringHolder) {
			return;
		}

		auto* factory = factoryManager->GetCreator<RE::MessageBoxData>(uiStringHolder->messageBoxData);
		if (!factory) {
			return;
		}

		auto* messageBoxData = factory->Create();
		if (!messageBoxData) {
			return;
		}

		messageBoxData->bodyText = bodyText;

		for (const auto& button : buttons) {
			messageBoxData->buttonText.push_back(button.c_str());
		}

		struct Callback : public RE::IMessageBoxCallback
		{
			std::function<void(unsigned int)> fn;
			explicit Callback(std::function<void(unsigned int)> a_fn) :
				fn(std::move(a_fn))
			{
				pad0C = 0;
			}
			~Callback() override = default;

			// Was Run(IMessageBoxCallback::Message) with a Message member; the interface
			// now hands the pressed button index straight through as a byte.
			void Run(std::uint8_t a_button) override
			{
				if (fn) {
					fn(static_cast<unsigned int>(a_button));
				}
			}
		};

		auto* cb = new Callback(std::move(callback));
		messageBoxData->callback.reset(cb);

		// QueueMessage() moved off the data object onto the menu class.
		RE::MessageBoxMenu::QueueMessage(messageBoxData);
	}
}
