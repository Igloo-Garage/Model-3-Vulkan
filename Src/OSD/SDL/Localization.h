/**
 ** Model 3-Vulkan
 ** A Vulkan-focused fork of Supermodel, the Sega Model 3 arcade emulator.
 ** Copyright (C) 2026 REXABYTE SOFTWARE INC. (Igloo-Garage / Rex)
 **
 ** This file is part of Model 3-Vulkan, and is derived from Supermodel,
 ** Copyright 2003-2026 The Supermodel Team.
 **
 ** Additional Terms under GNU GPLv3 Section 7 apply to this file
 ** (see LICENSE-ADDITIONAL-TERMS.md).
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/
#ifndef INCLUDED_LOCALIZATION_H
#define INCLUDED_LOCALIZATION_H

#include <string>

/*
 * Interface language.
 *
 * English is the default. The interface strings written into the source are Chinese, and those
 * same strings double as the lookup keys: when English is selected Tr() maps them through a table,
 * and when Chinese is selected Tr() hands the key straight back. Doing it that way means the
 * Chinese text stays where it is in the code, so adding a language cannot silently blank out a
 * label - the worst case is a string that has not been translated yet and still reads Chinese.
 *
 * Only two languages, no plural forms, no right-to-left and no date or number formatting, so this
 * is a plain table rather than the gettext/ICU machinery. Those exist to solve problems this
 * interface does not have, and would add a build step and a dependency for a few hundred strings.
 */

enum class UiLanguage
{
	English = 0,
	Chinese = 1
};

void SetUiLanguage(UiLanguage language);
UiLanguage GetUiLanguage();

// Name of a language as it should appear in its own language's interface.
const char* UiLanguageName(UiLanguage language);

/*
 * Translates one interface string.
 *
 * The argument is the Chinese source text, which is also the key. Strings with no entry come back
 * unchanged, so a partially translated interface degrades to Chinese rather than to blank labels.
 * The returned pointer always refers to a static string, so it stays valid for the lifetime of the
 * program - ImGui is free to keep it for as long as it likes.
 */
const char* Tr(const char* chinese);

/*
 * English source text -> Chinese, for strings that this program *originates* in English.
 *
 * Tr() is the other direction and only fires when the interface language is English, so it cannot
 * serve text whose source form is already English: in Chinese mode it returns its argument
 * untouched. The input names are exactly that case - CInput::GetInputGroup() and every CInput
 * label are written in English because the same strings are printed to the console and to the log,
 * where English is the only sensible form.
 *
 * Returns english unchanged in English mode, and unchanged in Chinese mode when the table has no
 * entry - so a missing row degrades to the English name rather than to a blank cell.
 */
const char* TrInputText(const char* english);

/*
 * Game names and version codes run the other way round.
 *
 * The titles come from Games.xml in English, so unlike the interface strings there is nothing in
 * the source to key on: these translate English to Chinese. Both return their input untouched when
 * English is selected, and when Chinese is selected an unknown name still comes back as English
 * rather than blank.
 */
const char* TrGameTitle(const char* englishTitle);

/*
 * Version codes, e.g. "Japan, Revision A", are built from comma-separated tokens that are translated
 * one at a time and joined back together, so a combination nobody has translated yet still shows
 * recognisably instead of being dropped.
 */
std::string TrGameVersion(const std::string& version);

#endif // INCLUDED_LOCALIZATION_H
