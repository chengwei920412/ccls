// Copyright 2017-2018 ccls Authors
// SPDX-License-Identifier: Apache-2.0

#include "message_handler.hh"
#include "query.hh"

namespace ccls {
namespace {
struct MarkedString {
  std::optional<std::string> language;
  std::string value;
};
struct Hover {
  std::vector<MarkedString> contents;
  std::optional<lsRange> range;
};

void Reflect(Writer &vis, MarkedString &v) {
  // If there is a language, emit a `{language:string, value:string}` object. If
  // not, emit a string.
  if (v.language) {
    REFLECT_MEMBER_START();
    REFLECT_MEMBER(language);
    REFLECT_MEMBER(value);
    REFLECT_MEMBER_END();
  } else {
    Reflect(vis, v.value);
  }
}
MAKE_REFLECT_STRUCT(Hover, contents, range);

const char *LanguageIdentifier(LanguageId lang) {
  switch (lang) {
  // clang-format off
  case LanguageId::C: return "c";
  case LanguageId::Cpp: return "cpp";
  case LanguageId::ObjC: return "objective-c";
  case LanguageId::ObjCpp: return "objective-cpp";
  default: return "";
  // clang-format on
  }
}

// Returns the hover or detailed name for `sym`, if any.
std::pair<std::optional<MarkedString>, std::optional<MarkedString>>
GetHover(DB *db, LanguageId lang, SymbolRef sym, int file_id) {
  const char *comments = nullptr;
  std::optional<MarkedString> ls_comments, hover;
  WithEntity(db, sym, [&](const auto &entity) {
    std::remove_reference_t<decltype(entity.def[0])> *def = nullptr;
    for (auto &d : entity.def) {
      if (d.spell) {
        comments = d.comments[0] ? d.comments : nullptr;
        def = &d;
        if (d.spell->file_id == file_id)
          break;
      }
    }
    if (!def && entity.def.size()) {
      def = &entity.def[0];
      if (def->comments[0])
        comments = def->comments;
    }
    if (def) {
      MarkedString m;
      m.language = LanguageIdentifier(lang);
      if (def->hover[0]) {
        m.value = def->hover;
        hover = m;
      } else if (def->detailed_name[0]) {
        m.value = def->detailed_name;
        hover = m;
      }
      if (comments)
        ls_comments = MarkedString{std::nullopt, comments};
    }
  });
  return {hover, ls_comments};
}
} // namespace

void MessageHandler::textDocument_hover(TextDocumentPositionParam &param,
                                        ReplyOnce &reply) {
  QueryFile *file = FindFile(reply, param.textDocument.uri.GetPath());
  WorkingFile *wf = file ? wfiles->GetFile(file->def->path) : nullptr;
  if (!wf)
    return;
  Hover result;

  for (SymbolRef sym : FindSymbolsAtLocation(wf, file, param.position)) {
    std::optional<lsRange> ls_range =
        GetLsRange(wfiles->GetFile(file->def->path), sym.range);
    if (!ls_range)
      continue;

    auto [hover, comments] = GetHover(db, file->def->language, sym, file->id);
    if (comments || hover) {
      result.range = *ls_range;
      if (comments)
        result.contents.push_back(*comments);
      if (hover)
        result.contents.push_back(*hover);
      break;
    }
  }

  reply(result);
}
} // namespace ccls
