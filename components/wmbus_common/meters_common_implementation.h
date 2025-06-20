/*
 Copyright (C) 2018-2022 Fredrik Öhrström (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef METERS_COMMON_IMPLEMENTATION_H_
#define METERS_COMMON_IMPLEMENTATION_H_

#include"dvparser.h"
#include"meters.h"
#include"units.h"

#include<map>
#include<set>


// Values in a meter are stored based on vname + Quantity.
// I.e. you can have a total_m3 and a total_kwh even though they share the same "total" vname
// since they have two different quantities (Volume and Energy).
// The field total_l refers to the same field storage in the meter as total_m3.
// If a wacko meter sends different values, one m3 and one l. then you
// have to name the fields using different vnames.
struct NumericField
{
    Unit unit {};
    double value {};
    FieldInfo *field_info {};
    DVEntry dv_entry {};

    NumericField() {}
    NumericField(Unit u, double v, FieldInfo *f) : unit(u), value(v), field_info(f) {}
    NumericField(Unit u, double v, FieldInfo *f, DVEntry &dve) : unit(u), value(v), field_info(f), dv_entry(dve) {}
};

struct StringField
{
    std::string value;
    FieldInfo *field_info {};

    StringField() {}
    StringField(std::string v, FieldInfo *f) : value(v), field_info(f) {}
};

struct MeterCommonImplementation : public virtual Meter
{
    int index();
    void setIndex(int i);
    std::string bus();
    std::vector<AddressExpression>& addressExpressions();
    IdentityMode identityMode();
    std::vector<FieldInfo> &fieldInfos();
    std::vector<std::string> &extraConstantFields();
    std::string name();
    DriverName driverName();
    DriverInfo *driverInfo();
    bool hasProcessContent();

    ELLSecurityMode expectedELLSecurityMode();
    TPLSecurityMode expectedTPLSecurityMode();

    std::string datetimeOfUpdateHumanReadable();
    std::string datetimeOfUpdateRobot();
    std::string unixTimestampOfUpdate();
    time_t timestampLastUpdate();
    void setPollInterval(time_t interval);
    time_t pollInterval();
    bool usesPolling();
    void addExtraCalculatedField(std::string ef);

    void onUpdate(function<void(Telegram*,Meter*)> cb);
    int numUpdates();

    static bool isTelegramForMeter(Telegram *t, Meter *meter, MeterInfo *mi);
    MeterKeys *meterKeys();

    MeterCommonImplementation(MeterInfo &mi, DriverInfo &di);

    ~MeterCommonImplementation() = default;

protected:

    void triggerUpdate(Telegram *t);
    void setExpectedELLSecurityMode(ELLSecurityMode dsm);
    void setExpectedTPLSecurityMode(TPLSecurityMode tsm);
    void addShellMeterAdded(std::string cmdline);
    void addShellMeterUpdated(std::string cmdline);
    void addExtraConstantField(std::string ecf);
    std::vector<std::string> &shellCmdlinesMeterAdded();
    std::vector<std::string> &shellCmdlinesMeterUpdated();
    std::vector<std::string> &meterExtraConstantFields();
    void setMeterType(MeterType mt);
    void addLinkMode(LinkMode lm);
    void setMfctTPLStatusBits(Translate::Lookup &lookup);

    void markLastFieldAsLibrary();

    void addNumericFieldWithExtractor(
        std::string vname,           // Name of value without unit, eg "total" "total_month{storagenr}"
        std::string help,            // Information about this field.
        PrintProperties print_properties, // Should this be printed by default in fields,json and hr.
        Quantity vquantity,     // Value belongs to this quantity, this quantity determines the default unit.
        VifScaling vif_scaling, // How should any Vif value be scaled.
        DifSignedness dif_signedness, // Should we override the default signed assumption for binary values?
        FieldMatcher matcher,
        Unit display_unit = Unit::Unknown, // If specified use this unit for the json field instead instead of the default unit.
        double scale = 1.0); // A hard coded extra scale factor. Useful for manufacturer specific values.

    void addNumericFieldWithCalculator(
        std::string vname,           // Name of value without unit, eg "total" "total_month{storagenr}"
        std::string help,            // Information about this field.
        PrintProperties print_properties, // Should this be printed by default in fields,json and hr.
        Quantity vquantity,     // Value belongs to this quantity, this quantity determines the default unit.
        std::string formula,         // The formula can reference the other fields and + them together.
        Unit display_unit = Unit::Unknown); // If specified use this unit for the json field instead instead of the default unit.

    void addNumericFieldWithCalculatorAndMatcher(
        std::string vname,           // Name of value without unit, eg "total" "total_month{storagenr}"
        std::string help,            // Information about this field.
        PrintProperties print_properties, // Should this be printed by default in fields,json and hr.
        Quantity vquantity,     // Value belongs to this quantity, this quantity determines the default unit.
        std::string formula,         // The formula can reference the other fields and + them together.
        FieldMatcher matcher,   // We can generate a calculated field per match.
        Unit display_unit = Unit::Unknown); // If specified use this unit for the json field instead instead of the default unit.

    void addNumericField(
        std::string vname,          // Name of value without unit, eg total
        Quantity vquantity,    // Value belongs to this quantity.
        PrintProperties print_properties, // Should this be printed by default in fields,json and hr.
        std::string help,
        Unit display_unit = Unit::Unknown);  // If specified use this unit for the json field instead instead of the default unit.

    void addStringFieldWithExtractor(
        std::string vname,
        std::string help,
        PrintProperties print_properties,
        FieldMatcher matcher);

    void addStringFieldWithExtractorAndLookup(
        std::string vname,
        std::string help,
        PrintProperties print_properties,
        FieldMatcher matcher,
        Translate::Lookup lookup); // Translate the bits/indexes.

    // Used only for status field from tpl_status only.
    void addStringField(
        std::string vname,
        std::string help,
        PrintProperties print_properties);

    bool handleTelegram(AboutTelegram &about, std::vector<uchar> frame,
                        bool simulated, std::vector<Address> *addresses,
                        bool *id_match, Telegram *out_analyzed = NULL);
    void createMeterEnv(std::string id,
                        std::vector<std::string> *envs,
                        std::vector<std::string> *more_json); // Add this json "key"="value" std::strings.
    void printMeter(Telegram *t,
                    std::string *human_readable,
                    std::string *fields, char separator,
                    std::string *json,
                    std::vector<std::string> *envs,
                    std::vector<std::string> *more_json, // Add this json "key"="value" std::strings.
                    std::vector<std::string> *selected_fields, // Only print these fields.
                    bool pretty_print); // Insert newlines and indentation.
    // Json fields include all values except timestamp_ut, timestamp_utc, timestamp_lt
    // since Json is assumed to be decoded by a program and the current timestamp which is the
    // same as timestamp_utc, can always be decoded/recoded into local time or a unix timestamp.

    FieldInfo *findFieldInfo(std::string vname, Quantity xuantity);
    std::string renderJsonOnlyDefaultUnit(std::string vname, Quantity xuantity);
    std::string debugValues();

    void processFieldExtractors(Telegram *t);
    void processFieldCalculators();
    std::string getStatusField(FieldInfo *fi);

    virtual void processContent(Telegram *t);

    void setNumericValue(std::string vname, Unit u, double v);
    void setNumericValue(FieldInfo *fi, DVEntry *dve, Unit u, double v);
    double getNumericValue(std::string vname, Unit u);
    double getNumericValue(FieldInfo *fi, Unit u);
    void setStringValue(std::string vname, std::string v, DVEntry *dve = NULL);
    void setStringValue(FieldInfo *fi, std::string v, DVEntry *dve);
    std::string getStringValue(FieldInfo *fi);

    // Check if the meter has received a value for this field.
    bool hasValue(FieldInfo *fi);
    bool hasNumericValue(FieldInfo *fi);
    bool hasStringValue(FieldInfo *fi);

    std::string decodeTPLStatusByte(uchar sts);

    bool addOptionalLibraryFields(std::string fields);

    std::vector<std::string> &selectedFields() { return selected_fields_; }
    void setSelectedFields (std::vector<std::string> &f) { selected_fields_ = f; }

    void forceMfctIndex(int i) { force_mfct_index_  = i; }
    bool hasReceivedFirstTelegram() {  return has_received_first_telegram_; }
    void markFirstTelegramReceived() { has_received_first_telegram_ = true; }

private:

    int index_ {};
    MeterType type_ {};
    DriverName driver_name_;
    DriverInfo *driver_info_ {};
    std::string bus_ {};
    MeterKeys meter_keys_ {};
    ELLSecurityMode expected_ell_sec_mode_ {};
    TPLSecurityMode expected_tpl_sec_mode_ {};
    std::string name_;
    std::vector<AddressExpression> address_expressions_;
    IdentityMode identity_mode_;
    int num_updates_ {};
    time_t datetime_of_update_ {};
    time_t datetime_of_poll_ {};
    LinkModeSet link_modes_ {};
    std::vector<std::string> shell_cmdlines_added_;
    std::vector<std::string> shell_cmdlines_updated_;
    std::vector<std::string> extra_constant_fields_;
    time_t poll_interval_ {};
    Translate::Lookup mfct_tpl_status_bits_ = NoLookup;
    int force_mfct_index_ = -1;
    bool has_process_content_ = false;
    bool has_received_first_telegram_ = false;

protected:

    std::vector<FieldInfo> field_infos_;
    // This is the number of fields in the driver, not counting the used library fields.
    size_t num_driver_fields_ {};
    std::vector<std::string> field_names_;
    // Defaults to a setting specified in the driver. Can be overridden in the meter file.
    // There is also a global selected_fields that can be set on the command line or in the conf file.
    std::vector<std::string> selected_fields_;
    // Map difvif key to hex values from telegrams.
    std::map<std::string,std::pair<int,std::string>> hex_values_;
    // Map field name+Unit to Numeric field which includes the value.
    std::map<std::pair<std::string,Unit>,NumericField> numeric_values_;
    // Map field name (at_date) to std::string value.
    std::map<std::string,StringField> string_values_;
    // If the telegram ends with 0x1f then set this to true, and the poll
    // code will poll again with 0x7b instead of 0x5b.
    bool more_records_follow_;
};

#endif
