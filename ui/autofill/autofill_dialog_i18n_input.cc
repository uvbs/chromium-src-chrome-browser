// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/autofill/autofill_dialog_i18n_input.h"

#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/chrome_switches.h"
#include "components/autofill/core/browser/field_types.h"
#include "grit/component_strings.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/address_field.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/address_ui.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/address_ui_component.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/localization.h"
#include "ui/base/l10n/l10n_util.h"

namespace autofill {
namespace i18ninput {

namespace {

using i18n::addressinput::AddressField;
using i18n::addressinput::AddressUiComponent;

ServerFieldType GetServerType(AddressField address_field, bool billing) {
  switch (address_field) {
    case i18n::addressinput::COUNTRY:
      return billing ? ADDRESS_BILLING_COUNTRY : ADDRESS_HOME_COUNTRY;
    case i18n::addressinput::ADMIN_AREA:
      return billing ? ADDRESS_BILLING_STATE : ADDRESS_HOME_STATE;
    case i18n::addressinput::LOCALITY:
      return billing ? ADDRESS_BILLING_CITY : ADDRESS_HOME_CITY;
    case i18n::addressinput::DEPENDENT_LOCALITY:
      return billing ? ADDRESS_BILLING_DEPENDENT_LOCALITY :
                       ADDRESS_HOME_DEPENDENT_LOCALITY;
    case i18n::addressinput::POSTAL_CODE:
      return billing ? ADDRESS_BILLING_ZIP : ADDRESS_HOME_ZIP;
    case i18n::addressinput::SORTING_CODE:
      return billing ? ADDRESS_BILLING_SORTING_CODE : ADDRESS_HOME_SORTING_CODE;
    case i18n::addressinput::STREET_ADDRESS:
      return billing ? ADDRESS_BILLING_LINE1 : ADDRESS_HOME_LINE1;
    case i18n::addressinput::RECIPIENT:
      return billing ? NAME_BILLING_FULL : NAME_FULL;
    case i18n::addressinput::ORGANIZATION:
      return COMPANY_NAME;
  }
  NOTREACHED();
  return UNKNOWN_TYPE;
}

DetailInput::Length LengthFromHint(AddressUiComponent::LengthHint hint) {
  if (hint == AddressUiComponent::HINT_SHORT)
    return DetailInput::SHORT;
  DCHECK_EQ(hint, AddressUiComponent::HINT_LONG);
  return DetailInput::LONG;
}

}  // namespace

bool Enabled() {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  return command_line->HasSwitch(::switches::kEnableAutofillAddressI18n);
}

void BuildAddressInputs(common::AddressType address_type,
                        const std::string& country_code,
                        DetailInputs* inputs) {
  i18n::addressinput::Localization localization;
  // TODO(dbeam): figure out how to include libaddressinput's translations into
  // some .pak file so I can call |SetGetter(&l10n_util::GetStringUTF8)| here.
  std::vector<AddressUiComponent> components(
      i18n::addressinput::BuildComponents(country_code, localization));

  const bool billing = address_type == common::ADDRESS_TYPE_BILLING;

  for (size_t i = 0; i < components.size(); ++i) {
    const AddressUiComponent& component = components[i];
    if (component.field == i18n::addressinput::ORGANIZATION) {
      // TODO(dbeam): figure out when we actually need this.
      continue;
    }

    ServerFieldType server_type = GetServerType(component.field, billing);
    DetailInput::Length length = LengthFromHint(component.length_hint);
    base::string16 placeholder = base::UTF8ToUTF16(component.name);
    DetailInput input = { length, server_type, placeholder };
    inputs->push_back(input);

    if (component.field == i18n::addressinput::STREET_ADDRESS &&
        component.length_hint == AddressUiComponent::HINT_LONG) {
      // TODO(dbeam): support more than 2 address lines. http://crbug.com/324889
      ServerFieldType server_type =
          billing ? ADDRESS_BILLING_LINE2 : ADDRESS_HOME_LINE2;
      base::string16 placeholder = base::UTF8ToUTF16(component.name);
      DetailInput input = { length, server_type, placeholder };
      inputs->push_back(input);
    }
  }

  ServerFieldType server_type =
      billing ? ADDRESS_BILLING_COUNTRY : ADDRESS_HOME_COUNTRY;
  base::string16 placeholder_text =
      l10n_util::GetStringUTF16(IDS_AUTOFILL_FIELD_LABEL_COUNTRY);
  // TODO(dbeam): unhide so users can switch countries. http://crbug.com/331544
  DetailInput input = { DetailInput::NONE, server_type, placeholder_text };
  inputs->push_back(input);
}

}  // namespace i18ninput
}  // namespace autofill
