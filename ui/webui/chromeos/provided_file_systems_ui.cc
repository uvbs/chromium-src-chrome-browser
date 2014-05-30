// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/chromeos/provided_file_systems_ui.h"

#include <vector>

#include "base/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/chromeos/file_system_provider/provided_file_system_info.h"
#include "chrome/browser/chromeos/file_system_provider/provided_file_system_interface.h"
#include "chrome/browser/chromeos/file_system_provider/request_manager.h"
#include "chrome/browser/chromeos/file_system_provider/service.h"
#include "chrome/browser/chromeos/file_system_provider/service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/url_constants.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "grit/browser_resources.h"

using content::BrowserThread;

namespace chromeos {

namespace {

// Class to handle messages from chrome://provided-file-systems.
class ProvidedFileSystemsWebUIHandler : public content::WebUIMessageHandler {
 public:
  ProvidedFileSystemsWebUIHandler() : weak_ptr_factory_(this) {}

  virtual ~ProvidedFileSystemsWebUIHandler() {}

 private:
  // content::WebUIMessageHandler overrides.
  virtual void RegisterMessages() OVERRIDE;

  // Gets a file system provider service for the current profile. If not found,
  // then NULL.
  file_system_provider::Service* GetService();

  // Invoked when updating file system list is requested.
  void OnUpdateFileSystems(const base::ListValue* args);

  base::WeakPtrFactory<ProvidedFileSystemsWebUIHandler> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ProvidedFileSystemsWebUIHandler);
};

void ProvidedFileSystemsWebUIHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "updateFileSystems",
      base::Bind(&ProvidedFileSystemsWebUIHandler::OnUpdateFileSystems,
                 weak_ptr_factory_.GetWeakPtr()));
}

file_system_provider::Service* ProvidedFileSystemsWebUIHandler::GetService() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  Profile* const profile = Profile::FromWebUI(web_ui());
  return file_system_provider::ServiceFactory::FindExisting(profile);
}

void ProvidedFileSystemsWebUIHandler::OnUpdateFileSystems(
    const base::ListValue* args) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  file_system_provider::Service* const service = GetService();
  if (!service)
    return;

  base::ListValue items;

  const std::vector<file_system_provider::ProvidedFileSystemInfo>
      file_system_info_list = service->GetProvidedFileSystemInfoList();

  for (size_t i = 0; i < file_system_info_list.size(); ++i) {
    const file_system_provider::ProvidedFileSystemInfo file_system_info =
        file_system_info_list[i];

    file_system_provider::ProvidedFileSystemInterface* const file_system =
        service->GetProvidedFileSystem(file_system_info.extension_id(),
                                       file_system_info.file_system_id());
    DCHECK(file_system);

    file_system_provider::RequestManager* const request_manager =
        file_system->GetRequestManager();
    DCHECK(request_manager);

    base::DictionaryValue* item_value = new base::DictionaryValue();
    item_value->SetString("id", file_system_info.file_system_id());
    item_value->SetString("name", file_system_info.file_system_name());
    item_value->SetString("extensionId", file_system_info.extension_id());
    item_value->SetString("mountPath",
                          file_system_info.mount_path().AsUTF8Unsafe());
    item_value->SetInteger("activeRequests",
                           request_manager->GetActiveRequestsForLogging());

    items.Append(item_value);
  }

  web_ui()->CallJavascriptFunction("updateFileSystems", items);
}

}  // namespace

ProvidedFileSystemsUI::ProvidedFileSystemsUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  web_ui->AddMessageHandler(new ProvidedFileSystemsWebUIHandler());

  content::WebUIDataSource* source = content::WebUIDataSource::Create(
      chrome::kChromeUIProvidedFileSystemsHost);
  source->AddResourcePath("provided_file_systems.css",
                          IDR_PROVIDED_FILE_SYSTEMS_CSS);
  source->AddResourcePath("provided_file_systems.js",
                          IDR_PROVIDED_FILE_SYSTEMS_JS);
  source->SetDefaultResource(IDR_PROVIDED_FILE_SYSTEMS_HTML);

  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource::Add(profile, source);
}

}  // namespace chromeos