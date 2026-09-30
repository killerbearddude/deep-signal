#include "save/SitePersistence.h"

// Site portion of the explicit v18 schema. Repeated fixed material channels use the current
// enum's stable ordinal as a column suffix; changing that enum requires a new
// schema. There are no dynamic player-supplied SQL identifiers or migrations.
#include <string>
#include <vector>

namespace deep::save {
namespace {
struct Column {
    std::string name;
    char kind;
};
using Columns = std::vector<Column>;
void materialColumns(Columns& columns, const char* prefix, const bool nullable = false) {
    for (std::size_t n = 0; n < processedMaterialCount(); ++n)
        columns.push_back({std::string{prefix} + std::to_string(n), nullable ? 'N' : 'n'});
}
void operatingPolicy(Columns& columns) {
    for (const Column& column : Columns{{"leader_id", 'I'},
                                        {"enabled", 'i'},
                                        {"requested_ice", 'n'},
                                        {"reactor_floor", 'n'},
                                        {"composites_floor", 'n'},
                                        {"duty_allowance", 'N'}})
        columns.push_back(column);
}
void developmentPolicy(Columns& columns) {
    columns.insert(columns.end(),
                   {{"propellant_floor", 'n'}, {"propellant_allowance", 'N'}, {"return_contingency", 'n'}});
    materialColumns(columns, "material_floor_");
    materialColumns(columns, "material_allowance_", true);
}
// Uppercase kinds permit NULL; numerical readers still enforce finite values.
// These helpers only expand the explicit table declarations immediately below.
void table(Database& db, const char* name, const Columns& columns, const std::string& keys) {
    std::string sql = "CREATE TABLE " + std::string{name} + " (";
    for (const auto& column : columns) {
        const bool optional = column.kind == 'I' || column.kind == 'N';
        const bool integral = column.kind == 'i' || column.kind == 'I';
        sql += column.name + (column.kind == 't' ? " TEXT" : integral ? " INTEGER" : " REAL");
        if (!optional)
            sql += " NOT NULL";
        if (column.name == "ordinal")
            sql += " CHECK(ordinal >= 0)";
        if (column.kind != 't') {
            sql += " CHECK(";
            if (optional)
                sql += column.name + " IS NULL OR ";
            sql += "typeof(" + column.name + ") " + (integral ? "= 'integer'" : "IN ('integer','real')");
            sql += ')';
        }
        sql += ',';
    }
    db.execute(sql + keys + ");");
}
} // namespace

void createSiteSchema(Database& db) {
    Columns catalog{{"ordinal", 'i'},          {"kind", 'i'},
                    {"version", 'i'},          {"assembly_work", 'n'},
                    {"power_generation", 'n'}, {"power_demand", 'n'},
                    {"extraction", 'n'},       {"handling", 'n'},
                    {"raw_storage", 'n'},      {"supported_extractors", 'n'},
                    {"reactor_per_duty", 'n'}, {"composites_per_duty", 'n'}};
    materialColumns(catalog, "cost_");
    table(db, "site_module_catalog", catalog, "PRIMARY KEY(version,kind), UNIQUE(ordinal)");
    table(db, "site_construction_binding", {{"singleton", 'i'}, {"family_id", 'I'}},
          "PRIMARY KEY(singleton), CHECK(singleton=1), FOREIGN KEY(family_id) REFERENCES "
          "equipment_families(id)");
    table(db, "colony_processing_totals", {{"colony_id", 'i'}, {"material", 'i'}, {"amount", 'n'}},
          "PRIMARY KEY(colony_id,material), FOREIGN KEY(colony_id) REFERENCES colonies(id)");

    Columns site{{"id", 'i'},
                 {"ordinal", 'i'},
                 {"body_id", 'i'},
                 {"name", 't'},
                 {"created_day", 'i'},
                 {"institution_id", 'I'},
                 {"operating_revision", 'i'},
                 {"report_start", 'i'},
                 {"next_report", 'i'},
                 {"issue_cause", 'i'},
                 {"episode_day", 'i'},
                 {"issue_message", 't'},
                 {"issue_acknowledged", 'i'}};
    operatingPolicy(site);
    materialColumns(site, "processed_");
    for (std::size_t n = 0; n < mineralCount(); ++n)
        site.push_back({"raw_" + std::to_string(n), 'n'});
    table(db, "resource_sites", site,
          "PRIMARY KEY(id), UNIQUE(ordinal), FOREIGN KEY(body_id) REFERENCES bodies(id),"
          "FOREIGN KEY(institution_id) REFERENCES institutions(id), FOREIGN KEY(leader_id) REFERENCES "
          "people(id)");

    Columns program{{"id", 'i'},
                    {"ordinal", 'i'},
                    {"site_id", 'i'},
                    {"support_colony_id", 'i'},
                    {"catalog_version", 'i'},
                    {"name", 't'},
                    {"requested_builder_id", 'I'},
                    {"requested_team_id", 'I'},
                    {"requested_leader_id", 'I'},
                    {"created_day", 'i'},
                    {"charter_revision", 'i'},
                    {"lifecycle", 'i'},
                    {"closure", 'i'},
                    {"closed_day", 'I'},
                    {"leased_builder_id", 'I'},
                    {"leased_team_id", 'I'},
                    {"holds_site", 'i'},
                    {"task", 'i'},
                    {"task_builder_id", 'I'},
                    {"task_team_id", 'I'},
                    {"commissioning_work", 'n'},
                    {"commissioning_workshop_id", 'I'},
                    {"commissioned_day", 'I'},
                    {"fuel_loaded", 'n'},
                    {"fuel_burned", 'n'},
                    {"report_start", 'i'},
                    {"next_report", 'i'},
                    {"issue_signature", 't'},
                    {"issue_message", 't'},
                    {"issue_acknowledged", 'i'}};
    developmentPolicy(program);
    table(db, "site_development_programs", program,
          "PRIMARY KEY(id), UNIQUE(ordinal), FOREIGN KEY(site_id) REFERENCES resource_sites(id),"
          "FOREIGN KEY(support_colony_id) REFERENCES colonies(id),"
          "FOREIGN KEY(requested_builder_id) REFERENCES fleets(id), FOREIGN KEY(requested_team_id) "
          "REFERENCES maintenance_teams(id),"
          "FOREIGN KEY(requested_leader_id) REFERENCES people(id), FOREIGN KEY(leased_builder_id) REFERENCES "
          "fleets(id),"
          "FOREIGN KEY(leased_team_id) REFERENCES maintenance_teams(id), FOREIGN KEY(task_builder_id) "
          "REFERENCES fleets(id),"
          "FOREIGN KEY(task_team_id) REFERENCES maintenance_teams(id), FOREIGN "
          "KEY(commissioning_workshop_id) REFERENCES ships(id)");

    Columns rows{{"program_id", 'i'}, {"ordinal", 'i'},        {"kind", 'i'},
                 {"quantity", 'i'},   {"work_completed", 'n'}, {"workshop_ship_id", 'I'}};
    materialColumns(rows, "consumed_");
    table(db, "site_development_rows", rows,
          "PRIMARY KEY(program_id,ordinal), UNIQUE(program_id,kind),"
          "FOREIGN KEY(program_id) REFERENCES site_development_programs(id), FOREIGN KEY(workshop_ship_id) "
          "REFERENCES ships(id)");
    table(db, "site_installed_modules",
          {{"site_id", 'i'},
           {"ordinal", 'i'},
           {"program_id", 'i'},
           {"package_row", 'i'},
           {"catalog_version", 'i'},
           {"kind", 'i'},
           {"quantity", 'i'},
           {"commissioned_day", 'i'}},
          "PRIMARY KEY(site_id,ordinal), UNIQUE(program_id,package_row), FOREIGN KEY(site_id) REFERENCES "
          "resource_sites(id),"
          "FOREIGN KEY(program_id,package_row) REFERENCES site_development_rows(program_id,ordinal),"
          "FOREIGN KEY(catalog_version,kind) REFERENCES site_module_catalog(version,kind)");

    Columns work{{"program_id", 'i'}, {"ordinal", 'i'},     {"day", 'i'},        {"revision", 'i'},
                 {"kind", 'i'},       {"package_row", 'i'}, {"builder_id", 'i'}, {"workshop_id", 'i'},
                 {"team_id", 'i'},    {"leader_id", 'i'},   {"work", 'n'}};
    materialColumns(work, "consumed_");
    table(db, "site_development_work", work,
          "PRIMARY KEY(program_id,ordinal), FOREIGN KEY(program_id) REFERENCES site_development_programs(id),"
          "FOREIGN KEY(builder_id) REFERENCES fleets(id), FOREIGN KEY(workshop_id) REFERENCES ships(id),"
          "FOREIGN KEY(team_id) REFERENCES maintenance_teams(id), FOREIGN KEY(leader_id) REFERENCES "
          "people(id)");
    Columns reports{{"program_id", 'i'}, {"ordinal", 'i'}, {"start_day", 'i'},
                    {"end_day", 'i'},    {"review", 'i'},  {"revision", 'i'}};
    developmentPolicy(reports);
    reports.insert(reports.end(), {{"assembly_work", 'n'}, {"commissioning_work", 'n'}});
    materialColumns(reports, "consumed_");
    reports.insert(reports.end(), {{"fuel_loaded", 'n'},
                                   {"fuel_burned", 'n'},
                                   {"builder_id", 'I'},
                                   {"team_id", 'I'},
                                   {"body_id", 'I'},
                                   {"commissioned", 'i'},
                                   {"waiting_reason", 't'},
                                   {"audit_through_id", 'i'}});
    table(db, "site_development_reports", reports,
          "PRIMARY KEY(program_id,ordinal), UNIQUE(program_id,end_day), FOREIGN KEY(program_id) REFERENCES "
          "site_development_programs(id),"
          "FOREIGN KEY(builder_id) REFERENCES fleets(id), FOREIGN KEY(team_id) REFERENCES "
          "maintenance_teams(id), FOREIGN KEY(body_id) REFERENCES bodies(id)");

    Columns duty{{"site_id", 'i'},
                 {"ordinal", 'i'},
                 {"day", 'i'},
                 {"revision", 'i'},
                 {"responsible_leader_id", 'i'},
                 {"installed_cutoff", 'i'},
                 {"duty", 'n'},
                 {"reactor_fuel", 'n'},
                 {"composites", 'n'},
                 {"raw_handling", 'n'}};
    operatingPolicy(duty);
    table(db, "site_duty_receipts", duty,
          "PRIMARY KEY(site_id,ordinal), UNIQUE(site_id,day), FOREIGN KEY(site_id) REFERENCES "
          "resource_sites(id),"
          "FOREIGN KEY(responsible_leader_id) REFERENCES people(id), FOREIGN KEY(leader_id) REFERENCES "
          "people(id)");
    table(db, "site_extraction_receipts",
          {{"site_id", 'i'},
           {"ordinal", 'i'},
           {"day", 'i'},
           {"revision", 'i'},
           {"leader_id", 'i'},
           {"installed_cutoff", 'i'},
           {"nominal_attempt", 'n'},
           {"recovered_ice", 'n'},
           {"free_raw_room", 'n'},
           {"available_handling", 'n'},
           {"observation", 't'}},
          "PRIMARY KEY(site_id,ordinal), UNIQUE(site_id,day), FOREIGN KEY(site_id) REFERENCES "
          "resource_sites(id), FOREIGN KEY(leader_id) REFERENCES people(id)");
    Columns operationReports{{"site_id", 'i'}, {"ordinal", 'i'}, {"start_day", 'i'},
                             {"end_day", 'i'}, {"review", 'i'},  {"revision", 'i'}};
    operatingPolicy(operationReports);
    operationReports.insert(operationReports.end(), {{"installed_cutoff", 'i'},
                                                     {"supported_duty", 'n'},
                                                     {"reactor_fuel", 'n'},
                                                     {"composites", 'n'},
                                                     {"attempts", 'i'},
                                                     {"recovered_ice", 'n'},
                                                     {"raw_occupancy", 'n'},
                                                     {"raw_capacity", 'n'},
                                                     {"exported_ice", 'n'},
                                                     {"delivered_ice", 'n'},
                                                     {"waiting_reason", 't'},
                                                     {"audit_through_id", 'i'}});
    table(db, "site_operating_reports", operationReports,
          "PRIMARY KEY(site_id,ordinal), UNIQUE(site_id,end_day), FOREIGN KEY(site_id) REFERENCES "
          "resource_sites(id), FOREIGN KEY(leader_id) REFERENCES people(id)");
}
} // namespace deep::save
