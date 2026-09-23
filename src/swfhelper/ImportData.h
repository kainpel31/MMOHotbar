// -----------------------------------------------------------------------------
// Adapted from Dynamic Inventory Icon Injector (DIII) by JerryYOJ.
//   https://github.com/JerryYOJ/Dynamic-Inventory-Icon-Injector-SKSE
// Copyright (C) JerryYOJ. Licensed under the GNU General Public License v3.0.
//
// This file is part of STB Hotkey System, which is therefore also distributed
// under the GPL-3.0. See LICENSE at the repository root.
// -----------------------------------------------------------------------------


namespace ImportData {
	struct loadReq {
		std::string_view sourcePath;
		std::vector<std::string_view> exports;
		std::vector<RE::GFxResource*> resources{}; //Caller leave it empty
	};

	RE::GFxMovieDefImpl* GetMovieDefImpl(RE::GFxMovieDef* movie);

	RE::GFxMovieDefImpl* LoadMovie(const std::string_view& a_sourcePath);
	bool ImportResources(RE::GFxMovieDefImpl* targetMovie, std::vector<loadReq>& Requests);
}