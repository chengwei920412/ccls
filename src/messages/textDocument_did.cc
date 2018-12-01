// Copyright 2017-2018 ccls Authors
// SPDX-License-Identifier: Apache-2.0

#include "include_complete.hh"
#include "message_handler.hh"
#include "pipeline.hh"
#include "project.hh"
#include "sema_manager.hh"
#include "working_files.hh"

namespace ccls {
void MessageHandler::textDocument_didChange(TextDocumentDidChangeParam &param) {
  std::string path = param.textDocument.uri.GetPath();
  wfiles->OnChange(param);
  if (g_config->index.onChange)
    pipeline::Index(path, {}, IndexMode::OnChange);
  manager->OnView(path);
  if (g_config->diagnostics.onChange >= 0)
    manager->ScheduleDiag(path, g_config->diagnostics.onChange);
}

void MessageHandler::textDocument_didClose(TextDocumentParam &param) {
  std::string path = param.textDocument.uri.GetPath();
  wfiles->OnClose(path);
  manager->OnClose(path);
}

void MessageHandler::textDocument_didOpen(DidOpenTextDocumentParam &param) {
  std::string path = param.textDocument.uri.GetPath();
  WorkingFile *wf = wfiles->OnOpen(param.textDocument);
  if (std::optional<std::string> cached_file_contents =
          pipeline::LoadIndexedContent(path))
    wf->SetIndexContent(*cached_file_contents);

  ReplyOnce reply;
  QueryFile *file = FindFile(reply, path);
  if (file) {
    EmitSkippedRanges(wf, *file);
    EmitSemanticHighlight(db, wf, *file);
  }
  include_complete->AddFile(wf->filename);

  // Submit new index request if it is not a header file or there is no
  // pending index request.
  std::pair<LanguageId, bool> lang = lookupExtension(path);
  if ((lang.first != LanguageId::Unknown && !lang.second) ||
      !pipeline::pending_index_requests)
    pipeline::Index(path, {}, IndexMode::Normal);

  manager->OnView(path);
}

void MessageHandler::textDocument_didSave(TextDocumentParam &param) {
  const std::string &path = param.textDocument.uri.GetPath();
  pipeline::Index(path, {}, IndexMode::Normal);
  manager->OnSave(path);
}
} // namespace ccls
