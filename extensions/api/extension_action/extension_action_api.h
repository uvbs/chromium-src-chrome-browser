// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_API_H_
#define CHROME_BROWSER_EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_API_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/scoped_observer.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "chrome/browser/extensions/extension_action.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "extensions/browser/browser_context_keyed_api_factory.h"
#include "extensions/browser/extension_registry_observer.h"

namespace base {
class DictionaryValue;
}

namespace content {
class BrowserContext;
class WebContents;
}

namespace extensions {
class ExtensionPrefs;
class ExtensionRegistry;
class TabHelper;

class ExtensionActionAPI : public BrowserContextKeyedAPI {
 public:
  class Observer {
   public:
    // Called when there is a change to the given |extension_action|.
    // |web_contents| is the web contents that was affected, and
    // |browser_context| is the associated BrowserContext. (The latter is
    // included because ExtensionActionAPI is shared between normal and
    // incognito contexts, so |browser_context| may not equal
    // |browser_context_|.)
    virtual void OnExtensionActionUpdated(
        ExtensionAction* extension_action,
        content::WebContents* web_contents,
        content::BrowserContext* browser_context) = 0;

    // Called when the ExtensionActionAPI is shutting down, giving observers a
    // chance to unregister themselves if there is not a definitive lifecycle.
    virtual void OnExtensionActionAPIShuttingDown() {}

   protected:
    virtual ~Observer() {}
  };

  explicit ExtensionActionAPI(content::BrowserContext* context);
  virtual ~ExtensionActionAPI();

  // Convenience method to get the instance for a profile.
  static ExtensionActionAPI* Get(content::BrowserContext* context);

  static bool GetBrowserActionVisibility(const ExtensionPrefs* prefs,
                                         const std::string& extension_id);
  static void SetBrowserActionVisibility(ExtensionPrefs* prefs,
                                         const std::string& extension_id,
                                         bool visible);

  // Fires the onClicked event for page_action.
  static void PageActionExecuted(content::BrowserContext* context,
                                 const ExtensionAction& page_action,
                                 int tab_id,
                                 const std::string& url,
                                 int button);

  // Fires the onClicked event for browser_action.
  static void BrowserActionExecuted(content::BrowserContext* context,
                                    const ExtensionAction& browser_action,
                                    content::WebContents* web_contents);

  // BrowserContextKeyedAPI implementation.
  static BrowserContextKeyedAPIFactory<ExtensionActionAPI>*
      GetFactoryInstance();

  // Add or remove observers.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  void NotifyChange(ExtensionAction* extension_action,
                    content::WebContents* web_contents,
                    content::BrowserContext* browser_context);

  // Clears the values for all ExtensionActions for the tab associated with the
  // given |web_contents|.
  void ClearAllValuesForTab(content::WebContents* web_contents);

 private:
  friend class BrowserContextKeyedAPIFactory<ExtensionActionAPI>;

  // The DispatchEvent methods forward events to the |profile|'s event router.
  static void DispatchEventToExtension(content::BrowserContext* context,
                                       const std::string& extension_id,
                                       const std::string& event_name,
                                       scoped_ptr<base::ListValue> event_args);

  // Called when either a browser or page action is executed. Figures out which
  // event to send based on what the extension wants.
  static void ExtensionActionExecuted(content::BrowserContext* context,
                                      const ExtensionAction& extension_action,
                                      content::WebContents* web_contents);

  // BrowserContextKeyedAPI implementation.
  virtual void Shutdown() OVERRIDE;
  static const char* service_name() { return "ExtensionActionAPI"; }
  static const bool kServiceRedirectedInIncognito = true;

  ObserverList<Observer> observers_;

  content::BrowserContext* browser_context_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionActionAPI);
};

// This class manages reading and writing browser action values from storage.
class ExtensionActionStorageManager
    : public ExtensionActionAPI::Observer,
      public ExtensionRegistryObserver,
      public base::SupportsWeakPtr<ExtensionActionStorageManager> {
 public:
  explicit ExtensionActionStorageManager(Profile* profile);
  virtual ~ExtensionActionStorageManager();

 private:
  // ExtensionActionAPI::Observer:
  virtual void OnExtensionActionUpdated(
      ExtensionAction* extension_action,
      content::WebContents* web_contents,
      content::BrowserContext* browser_context) OVERRIDE;
  virtual void OnExtensionActionAPIShuttingDown() OVERRIDE;

  // ExtensionRegistryObserver:
  virtual void OnExtensionLoaded(content::BrowserContext* browser_context,
                                 const Extension* extension) OVERRIDE;

  // Reads/Writes the ExtensionAction's default values to/from storage.
  void WriteToStorage(ExtensionAction* extension_action);
  void ReadFromStorage(
      const std::string& extension_id, scoped_ptr<base::Value> value);

  Profile* profile_;

  ScopedObserver<ExtensionActionAPI, ExtensionActionAPI::Observer>
      extension_action_observer_;

  // Listen to extension loaded notification.
  ScopedObserver<ExtensionRegistry, ExtensionRegistryObserver>
      extension_registry_observer_;
};

// Implementation of the browserAction and pageAction APIs.
//
// Divergent behaviour between the two is minimal (pageAction has required
// tabIds while browserAction's are optional, they have different internal
// browser notification requirements, and not all functions are defined for all
// APIs).
class ExtensionActionFunction : public ChromeSyncExtensionFunction {
 public:
  static bool ParseCSSColorString(const std::string& color_string,
                                  SkColor* result);

 protected:
  ExtensionActionFunction();
  virtual ~ExtensionActionFunction();
  virtual bool RunSync() OVERRIDE;
  virtual bool RunExtensionAction() = 0;

  bool ExtractDataFromArguments();
  void NotifyChange();
  bool SetVisible(bool visible);

  // Extension-related information for |tab_id_|.
  // CHECK-fails if there is no tab.
  extensions::TabHelper& tab_helper() const;

  // All the extension action APIs take a single argument called details that
  // is a dictionary.
  base::DictionaryValue* details_;

  // The tab id the extension action function should apply to, if any, or
  // kDefaultTabId if none was specified.
  int tab_id_;

  // WebContents for |tab_id_| if one exists.
  content::WebContents* contents_;

  // The extension action for the current extension.
  ExtensionAction* extension_action_;
};

//
// Implementations of each extension action API.
//
// pageAction and browserAction bindings are created for these by extending them
// then declaring an EXTENSION_FUNCTION_NAME.
//

// show
class ExtensionActionShowFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionShowFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// hide
class ExtensionActionHideFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionHideFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// setIcon
class ExtensionActionSetIconFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionSetIconFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// setTitle
class ExtensionActionSetTitleFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionSetTitleFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// setPopup
class ExtensionActionSetPopupFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionSetPopupFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// setBadgeText
class ExtensionActionSetBadgeTextFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionSetBadgeTextFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// setBadgeBackgroundColor
class ExtensionActionSetBadgeBackgroundColorFunction
    : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionSetBadgeBackgroundColorFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// getTitle
class ExtensionActionGetTitleFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionGetTitleFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// getPopup
class ExtensionActionGetPopupFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionGetPopupFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// getBadgeText
class ExtensionActionGetBadgeTextFunction : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionGetBadgeTextFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

// getBadgeBackgroundColor
class ExtensionActionGetBadgeBackgroundColorFunction
    : public ExtensionActionFunction {
 protected:
  virtual ~ExtensionActionGetBadgeBackgroundColorFunction() {}
  virtual bool RunExtensionAction() OVERRIDE;
};

//
// browserAction.* aliases for supported browserAction APIs.
//

class BrowserActionSetIconFunction : public ExtensionActionSetIconFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setIcon", BROWSERACTION_SETICON)

 protected:
  virtual ~BrowserActionSetIconFunction() {}
};

class BrowserActionSetTitleFunction : public ExtensionActionSetTitleFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setTitle", BROWSERACTION_SETTITLE)

 protected:
  virtual ~BrowserActionSetTitleFunction() {}
};

class BrowserActionSetPopupFunction : public ExtensionActionSetPopupFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setPopup", BROWSERACTION_SETPOPUP)

 protected:
  virtual ~BrowserActionSetPopupFunction() {}
};

class BrowserActionGetTitleFunction : public ExtensionActionGetTitleFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.getTitle", BROWSERACTION_GETTITLE)

 protected:
  virtual ~BrowserActionGetTitleFunction() {}
};

class BrowserActionGetPopupFunction : public ExtensionActionGetPopupFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.getPopup", BROWSERACTION_GETPOPUP)

 protected:
  virtual ~BrowserActionGetPopupFunction() {}
};

class BrowserActionSetBadgeTextFunction
    : public ExtensionActionSetBadgeTextFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setBadgeText",
                             BROWSERACTION_SETBADGETEXT)

 protected:
  virtual ~BrowserActionSetBadgeTextFunction() {}
};

class BrowserActionSetBadgeBackgroundColorFunction
    : public ExtensionActionSetBadgeBackgroundColorFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.setBadgeBackgroundColor",
                             BROWSERACTION_SETBADGEBACKGROUNDCOLOR)

 protected:
  virtual ~BrowserActionSetBadgeBackgroundColorFunction() {}
};

class BrowserActionGetBadgeTextFunction
    : public ExtensionActionGetBadgeTextFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.getBadgeText",
                             BROWSERACTION_GETBADGETEXT)

 protected:
  virtual ~BrowserActionGetBadgeTextFunction() {}
};

class BrowserActionGetBadgeBackgroundColorFunction
    : public ExtensionActionGetBadgeBackgroundColorFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.getBadgeBackgroundColor",
                             BROWSERACTION_GETBADGEBACKGROUNDCOLOR)

 protected:
  virtual ~BrowserActionGetBadgeBackgroundColorFunction() {}
};

class BrowserActionEnableFunction : public ExtensionActionShowFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.enable", BROWSERACTION_ENABLE)

 protected:
  virtual ~BrowserActionEnableFunction() {}
};

class BrowserActionDisableFunction : public ExtensionActionHideFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.disable", BROWSERACTION_DISABLE)

 protected:
  virtual ~BrowserActionDisableFunction() {}
};

class BrowserActionOpenPopupFunction : public ChromeAsyncExtensionFunction,
                                       public content::NotificationObserver {
 public:
  DECLARE_EXTENSION_FUNCTION("browserAction.openPopup",
                             BROWSERACTION_OPEN_POPUP)
  BrowserActionOpenPopupFunction();

 private:
  virtual ~BrowserActionOpenPopupFunction() {}

  // ExtensionFunction:
  virtual bool RunAsync() OVERRIDE;

  virtual void Observe(int type,
                       const content::NotificationSource& source,
                       const content::NotificationDetails& details) OVERRIDE;
  void OpenPopupTimedOut();

  content::NotificationRegistrar registrar_;
  bool response_sent_;

  DISALLOW_COPY_AND_ASSIGN(BrowserActionOpenPopupFunction);
};

}  // namespace extensions

//
// pageAction.* aliases for supported pageAction APIs.
//

class PageActionShowFunction : public extensions::ExtensionActionShowFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.show", PAGEACTION_SHOW)

 protected:
  virtual ~PageActionShowFunction() {}
};

class PageActionHideFunction : public extensions::ExtensionActionHideFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.hide", PAGEACTION_HIDE)

 protected:
  virtual ~PageActionHideFunction() {}
};

class PageActionSetIconFunction
    : public extensions::ExtensionActionSetIconFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.setIcon", PAGEACTION_SETICON)

 protected:
  virtual ~PageActionSetIconFunction() {}
};

class PageActionSetTitleFunction
    : public extensions::ExtensionActionSetTitleFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.setTitle", PAGEACTION_SETTITLE)

 protected:
  virtual ~PageActionSetTitleFunction() {}
};

class PageActionSetPopupFunction
    : public extensions::ExtensionActionSetPopupFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.setPopup", PAGEACTION_SETPOPUP)

 protected:
  virtual ~PageActionSetPopupFunction() {}
};

class PageActionGetTitleFunction
    : public extensions::ExtensionActionGetTitleFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.getTitle", PAGEACTION_GETTITLE)

 protected:
  virtual ~PageActionGetTitleFunction() {}
};

class PageActionGetPopupFunction
    : public extensions::ExtensionActionGetPopupFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("pageAction.getPopup", PAGEACTION_GETPOPUP)

 protected:
  virtual ~PageActionGetPopupFunction() {}
};

#endif  // CHROME_BROWSER_EXTENSIONS_API_EXTENSION_ACTION_EXTENSION_ACTION_API_H_
