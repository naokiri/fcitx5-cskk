/*
 * Copyright (c) 2021 Naoaki Iwakiri
 * This program is released under GNU General Public License version 3 or later
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Creation Date: 2021-04-30
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileType: SOURCE
 * SPDX-FileName: cskk.cpp
 * SPDX-FileCopyrightText: Copyright (c) 2021 Naoaki Iwakiri
 */
#include "cskk.h"
#include "cskkcandidatelist.h"
#include "log.h"
#include <cstdlib>
#include <cstring>
#include <fcitx-config/iniparser.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputpanel.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
using std::getenv;
using std::string;

FCITX_DEFINE_LOG_CATEGORY(cskk_log, "cskk");

namespace fcitx {

/*******************************************************************************
 * FcitxCskkEngine
 ******************************************************************************/
const string FcitxCskkEngine::config_file_path = string{"conf/fcitx5-cskk"};

FcitxCskkEngine::FcitxCskkEngine(Instance *instance)
    : instance_{instance}, factory_([this](InputContext &ic) {
        auto newCskkContext = new FcitxCskkContext(this, &ic);
        newCskkContext->applyConfig();
        return newCskkContext;
      }) {
  reloadConfig();
  instance_->inputContextManager().registerProperty("cskkcontext", &factory_);
}
FcitxCskkEngine::~FcitxCskkEngine() = default;
void FcitxCskkEngine::keyEvent(const InputMethodEntry &, KeyEvent &keyEvent) {
  CSKK_DEBUG() << "Engine keyEvent start: " << keyEvent.rawKey();
  // delegate to context
  auto ic = keyEvent.inputContext();
  auto context = ic->propertyFor(&factory_);
  context->keyEvent(keyEvent);
  CSKK_DEBUG() << "Engine keyEvent end";
}
void FcitxCskkEngine::save() {
  if (factory_.registered()) {
    instance_->inputContextManager().foreach ([this](InputContext *ic) {
      auto context = ic->propertyFor(&factory_);
      skk_context_save_dictionaries(context->context());
      return true;
    });
  }
}
void FcitxCskkEngine::activate(const InputMethodEntry &, InputContextEvent &) {}
void FcitxCskkEngine::deactivate(const InputMethodEntry &entry,
                                 InputContextEvent &event) {
  FCITX_UNUSED(entry);
  reset(entry, event);
}
void FcitxCskkEngine::reset(const InputMethodEntry &entry,
                            InputContextEvent &event) {
  FCITX_UNUSED(entry);
  CSKK_DEBUG() << "Reset";
  auto ic = event.inputContext();
  auto context = ic->propertyFor(&factory_);
  context->reset();
}
void FcitxCskkEngine::setConfig(const RawConfig &config) {
  CSKK_DEBUG() << "Cskk setconfig";
  config_.load(config, true);
  safeSaveAsIni(config_, FcitxCskkEngine::config_file_path);
  reloadConfig();
}
void FcitxCskkEngine::reloadConfig() {
  CSKK_DEBUG() << "Cskkengine reload config";
  readAsIni(config_, FcitxCskkEngine::config_file_path);

  loadDictionary();
  if (factory_.registered()) {
    instance_->inputContextManager().foreach ([this](InputContext *ic) {
      auto context = ic->propertyFor(&factory_);
      context->applyConfig();
      return true;
    });
  }
}
void FcitxCskkEngine::loadDictionary() {
  // For time being, just load files from XDG_DATA_{HOME,DIRS} only.
  freeDictionaries();
  CSKK_DEBUG() << "Start loading dictionaries";
  const std::filesystem::directory_options directoryOptions =
      (std::filesystem::directory_options::skip_permission_denied |
       std::filesystem::directory_options::follow_directory_symlink);

  const auto &standardPath = StandardPath::global();
  std::string path;
  auto templatePath = "fcitx5-cskk/dictionary";
  standardPath.scanDirectories(
      StandardPath::Type::Data,
      [this, &templatePath](const std::string &dirPath, bool isUser) {
        auto dataDir = stringutils::joinPath(dirPath, templatePath);
        CSKK_DEBUG() << "Search " << dataDir << " for dicts";
        try {
          for (const auto &file :
               std::filesystem::directory_iterator(dataDir, directoryOptions)) {
            if (file.is_regular_file()) {
              if (isUser) {
                auto path = file.path().string();
                if (path.length() > 5) {
                  if (path.compare(path.length() - 5, 5, ".dict") == 0) {
                    dictionaries_.emplace_back(
                        skk_user_dict_new(file.path().c_str(), "UTF-8"));
                    CSKK_DEBUG() << "Register " << file << " as r/w dict";
                  }
                }
              } else {
                dictionaries_.emplace_back(
                    skk_file_dict_new(file.path().c_str(), "UTF-8"));
                CSKK_DEBUG() << "Register " << file << " as static dict";
              }
            }
          }
        } catch (std::filesystem::filesystem_error &ignored) {
          CSKK_WARN() << "Attempt to read dictionary failed. Skipping.";
        }
        return true;
      });
}

void FcitxCskkEngine::freeDictionaries() {
  CSKK_DEBUG() << "Cskk free dict";
  for (auto dictionary : dictionaries_) {
    skk_free_dictionary(dictionary);
  }
  dictionaries_.clear();
}
KeyList FcitxCskkEngine::getSelectionKeys(
    CandidateSelectionKeys candidateSelectionKeys) {
  switch (candidateSelectionKeys) {
  case CandidateSelectionKeys::ABCD:
    return fcitx::KeyList{Key(FcitxKey_a), Key(FcitxKey_b), Key(FcitxKey_c),
                          Key(FcitxKey_d), Key(FcitxKey_e), Key(FcitxKey_f),
                          Key(FcitxKey_g), Key(FcitxKey_h), Key(FcitxKey_i),
                          Key(FcitxKey_j)};
  case CandidateSelectionKeys::QwertyCenter:
    return fcitx::KeyList{Key(FcitxKey_a), Key(FcitxKey_s),
                          Key(FcitxKey_d), Key(FcitxKey_f),
                          Key(FcitxKey_g), Key(FcitxKey_h),
                          Key(FcitxKey_j), Key(FcitxKey_k),
                          Key(FcitxKey_l), Key(FcitxKey_semicolon)};
  case CandidateSelectionKeys::Number:
  default:
    return fcitx::KeyList{Key(FcitxKey_1), Key(FcitxKey_2), Key(FcitxKey_3),
                          Key(FcitxKey_4), Key(FcitxKey_5), Key(FcitxKey_6),
                          Key(FcitxKey_7), Key(FcitxKey_8), Key(FcitxKey_9),
                          Key(FcitxKey_0)};
  }
}
std::string FcitxCskkEngine::subModeIconImpl(const InputMethodEntry &,
                                             InputContext &ic) {
  auto context = ic.propertyFor(&factory_);
  auto current_input_mode = skk_context_get_input_mode(context->context());
  switch (current_input_mode) {
  case InputMode::Ascii:
    return "cskk-ascii";
  case InputMode::HankakuKatakana:
    return "cskk-hankakukana";
  case InputMode::Hiragana:
    return "cskk-hiragana";
  case InputMode::Katakana:
    return "cskk-katakana";
  case InputMode::Zenkaku:
    return "cskk-zenei";
  }
  return "";
}

/*******************************************************************************
 * CskkContext
 ******************************************************************************/

FcitxCskkContext::FcitxCskkContext(FcitxCskkEngine *engine, InputContext *ic)
    : context_(skk_context_new(nullptr, 0)), ic_(ic), engine_(engine) {
  CSKK_DEBUG() << "Cskk context new";
  skk_context_set_input_mode(context_, *engine_->config().inputMode);
}
FcitxCskkContext::~FcitxCskkContext() = default;
void FcitxCskkContext::keyEvent(KeyEvent &keyEvent) {
  auto candidateList = std::dynamic_pointer_cast<FcitxCskkCandidateList>(
      ic_->inputPanel().candidateList());
  if (candidateList != nullptr && !candidateList->empty()) {
    if (handleCandidateSelection(candidateList, keyEvent)) {
      updateUI();
      return;
    }
  }

  if (skk_context_get_composition_mode(context_) !=
      CompositionMode::CompositionSelection) {
    CSKK_DEBUG() << "not composition selection. destroy candidate list.";
    ic_->inputPanel().setCandidateList(nullptr);
  }

  uint32_t modifiers =
      static_cast<uint32_t>(keyEvent.rawKey().states() & KeyState::SimpleMask);
  CskkKeyEvent *cskkKeyEvent = skk_key_event_new_from_fcitx_keyevent(
      keyEvent.rawKey().sym(), modifiers, keyEvent.isRelease());

  if (skk_context_process_key_event(context_, cskkKeyEvent)) {
    CSKK_DEBUG() << "Key processed in context.";
    keyEvent.filterAndAccept();
  }

  if (keyEvent.filtered()) {
    updateUI();
  }
}
bool FcitxCskkContext::handleCandidateSelection(
    const std::shared_ptr<FcitxCskkCandidateList> &candidateList,
    KeyEvent &keyEvent) {
  if (keyEvent.isRelease()) {
    return false;
  }
  CSKK_DEBUG() << "handleCandidateSelection";
  const auto config = engine_->config();

  if (keyEvent.key().checkKeyList(*config.prevCursorKey)) {
    candidateList->prevCandidate();
    keyEvent.filterAndAccept();
  } else if (keyEvent.key().checkKeyList(*config.nextCursorKey)) {
    candidateList->nextCandidate();
    keyEvent.filterAndAccept();
  } else if (keyEvent.key().checkKeyList(*config.nextPageKey)) {
    candidateList->next();
    keyEvent.filterAndAccept();
  } else if (keyEvent.key().checkKeyList(*config.prevPageKey)) {
    candidateList->prev();
    keyEvent.filterAndAccept();
  } else if (keyEvent.key().check(Key(FcitxKey_Return))) {
    CSKK_DEBUG() << "return key caught in handle candidate";
    candidateList->candidate(candidateList->cursorIndex()).select(ic_);
    keyEvent.filterAndAccept();
  } else {
    KeyList selectionKeys = FcitxCskkEngine::getSelectionKeys(
        engine_->config().candidateSelectionKeys.value());
    if (auto idx = keyEvent.key().keyListIndex(selectionKeys);
        0 <= idx && idx < engine_->config().pageSize.value()) {
      CSKK_DEBUG() << "Select from page. Idx: " << idx;
      candidateList->candidate(idx).select(ic_);
      keyEvent.filterAndAccept();
    }
  }

  return keyEvent.filtered();
}

void FcitxCskkContext::reset() {
  skk_context_reset(context_);
  updateUI();
}
void FcitxCskkContext::updateUI() {
  auto &inputPanel = ic_->inputPanel();

  // Output
  if (auto output = skk_context_poll_output(context_)) {
    CSKK_DEBUG() << "output: " << output;
    if (strlen(output) > 0) {
      ic_->commitString(output);
    }
    skk_free_string(output);
  }

  // Preedit
  auto preedit = skk_context_get_preedit(context_);
  Text preeditText;
  // FIXME: Pretty text format someday.
  preeditText.append(std::string(preedit));
  preeditText.setCursor(static_cast<int>(strlen(preedit)));

  // CandidateList
  int currentCursorPosition =
      skk_context_get_current_candidate_cursor_position(context_);
  if (currentCursorPosition > engine_->config().pageStartIdx.value() - 1) {
    char *current_to_composite = skk_context_get_current_to_composite(context_);
    auto currentCandidateList =
        std::dynamic_pointer_cast<FcitxCskkCandidateList>(
            ic_->inputPanel().candidateList());
    if (currentCandidateList == nullptr || currentCandidateList->empty() ||
        (strcmp(currentCandidateList->to_composite().c_str(),
                current_to_composite) != 0)) {
      // update whole currentCandidateList only if needed
      CSKK_DEBUG() << "Set new candidate list on UI update";
      inputPanel.setCandidateList(
          std::make_unique<FcitxCskkCandidateList>(engine_, ic_));
    } else {
      // Sync UI with actual data
      currentCandidateList->setCursorPosition(
          static_cast<int>(currentCursorPosition));
    }

  } else {
    inputPanel.setCandidateList(nullptr);
  }

  if (ic_->capabilityFlags().test(CapabilityFlag::Preedit)) {
    inputPanel.setClientPreedit(preeditText);
    ic_->updatePreedit();
  } else {
    inputPanel.setPreedit(preeditText);
  }

  // StatusArea for status icon
  ic_->updateUserInterface(UserInterfaceComponent::StatusArea);
  ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
}
void FcitxCskkContext::applyConfig() {
  CSKK_DEBUG() << "apply config";
  auto &config = engine_->config();

  skk_context_set_dictionaries(context_, engine_->dictionaries().data(),
                               engine_->dictionaries().size());
  skk_context_set_period_style(context_, *config.periodStyle);
  skk_context_set_comma_style(context_, *config.commaStyle);
}
void FcitxCskkContext::copyTo(InputContextProperty *context) {
  auto otherContext = dynamic_cast<FcitxCskkContext *>(context);
  skk_context_set_input_mode(otherContext->context(),
                             skk_context_get_input_mode(context_));
}

/*******************************************************************************
 * FcitxCskkFactory
 ******************************************************************************/

AddonInstance *FcitxCskkFactory::create(AddonManager *manager) {
  {
    CSKK_DEBUG() << "**** CSKK FcitxCskkFactory Create ****";
    registerDomain("fcitx5-cskk", FCITX_INSTALL_LOCALEDIR);
    auto engine = new FcitxCskkEngine(manager->instance());
    return engine;
  }
}
} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::FcitxCskkFactory);
