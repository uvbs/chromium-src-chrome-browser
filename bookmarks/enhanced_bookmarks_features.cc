// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/bookmarks/enhanced_bookmarks_features.h"

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "base/sha1.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/common/chrome_switches.h"
#include "components/variations/variations_associated_data.h"
#include "extensions/common/features/feature.h"
#include "extensions/common/features/feature_provider.h"

namespace {

const char kFieldTrialName[] = "EnhancedBookmarks";

bool IsBookmarksExtensionHash(const std::string& sha1_hex) {
    return sha1_hex == "D5736E4B5CF695CB93A2FB57E4FDC6E5AFAB6FE2" ||
           sha1_hex == "D57DE394F36DC1C3220E7604C575D29C51A6C495";
}

};  // namespace

bool OptInIntoBookmarksExperimentIfHasExtension(
    const extensions::ExtensionIdSet& extension_ids) {
  if (base::FieldTrialList::FindFullName(kFieldTrialName) != "Default")
    return false;

  // Compare installed extension ids with ones we expect.
  for (extensions::ExtensionIdSet::const_iterator iter = extension_ids.begin();
       iter != extension_ids.end(); ++iter) {
    const std::string id_hash = base::SHA1HashString(*iter);
    DCHECK_EQ(id_hash.length(), base::kSHA1Length);
    std::string hash = base::HexEncode(id_hash.c_str(), id_hash.length());

    if (IsBookmarksExtensionHash(hash)) {
      // Enable features bookmarks depends on and opt-in user into Finch group
      // for reporting.
      CommandLine* command_line = CommandLine::ForCurrentProcess();
      command_line->AppendSwitch(switches::kManualEnhancedBookmarks);
      command_line->AppendSwitch(switches::kEnableSyncArticles);
      command_line->AppendSwitch(switches::kEnableDomDistiller);
      return true;
    }
  }
  return false;
}

bool IsEnhancedBookmarksExperimentEnabled() {
  std::string ext_id = GetEnhancedBookmarksExtensionId();
  extensions::FeatureProvider* feature_provider =
      extensions::FeatureProvider::GetPermissionFeatures();
  extensions::Feature* feature = feature_provider->GetFeature("metricsPrivate");
  return feature && feature->IsIdInWhitelist(ext_id);
}

bool IsEnableDomDistillerSet() {
  if (CommandLine::ForCurrentProcess()->
      HasSwitch(switches::kEnableDomDistiller)) {
    return true;
  }
  if (chrome_variations::GetVariationParamValue(
          kFieldTrialName, "enable-dom-distiller") == "1")
    return true;

  return false;
}

bool IsEnableSyncArticlesSet() {
  if (CommandLine::ForCurrentProcess()->
      HasSwitch(switches::kEnableSyncArticles)) {
    return true;
  }
  if (chrome_variations::GetVariationParamValue(
          kFieldTrialName, "enable-sync-articles") == "1")
    return true;

  return false;
}

std::string GetEnhancedBookmarksExtensionId() {
  return chrome_variations::GetVariationParamValue(kFieldTrialName, "id");
}
