#include "save/SitePersistence.h"

// Maps every authoritative site/development field in canonical table order.
// Ordered children use strict contiguous ordinals; fixed inventory channels are
// complete, typed arrays. Opening budgets and geological reserves are absent.
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace deep::save {
namespace {
std::string insertSql(const char* table, int columns) {
    std::string sql = "INSERT INTO " + std::string{table} + " VALUES(";
    for (int i = 0; i < columns; ++i)
        sql += i ? ",?" : "?";
    return sql + ");";
}
// Statement binding remains prepared and positional; only static table names
// enter SQL. The exact-count assertion catches an accidentally omitted field.
struct Writer {
    Statement statement;
    int next = 1;
    int count;
    Writer(Database& db, const char* table, int columns)
        : statement(db, insertSql(table, columns)), count(columns) {}
    void i(std::int64_t value) {
        statement.bindInt64(next++, value);
    }
    void n(double value) {
        statement.bindDouble(next++, value);
    }
    void t(const std::string& value) {
        statement.bindText(next++, value);
    }
    template <class Id> void id(Id value) {
        i(value.value);
    }
    template <class Enum> void e(Enum value) {
        i(static_cast<std::int64_t>(value));
    }
    template <class Id> void optionalId(const std::optional<Id>& value) {
        if (value)
            id(*value);
        else
            statement.bindNull(next++);
    }
    void optionalDay(const std::optional<std::int64_t>& value) {
        if (value)
            i(*value);
        else
            statement.bindNull(next++);
    }
    void optionalNumber(const std::optional<double>& value) {
        if (value)
            n(*value);
        else
            statement.bindNull(next++);
    }
    void materials(const ProcessedMaterialSet& value) {
        for (double amount : value.amount)
            n(amount);
    }
    void operating(const SiteOperatingPolicy& p) {
        optionalId(p.leaderId);
        i(p.enabled);
        n(p.requestedIcePerDay);
        n(p.reactorFuelFloor);
        n(p.compositesFloor);
        optionalNumber(p.lifetimeDutyAllowance);
    }
    void policy(const SiteDevelopmentPolicy& p) {
        n(p.homePropellantFloor);
        optionalNumber(p.additionalPropellantAllowance);
        n(p.returnContingencyFraction);
        materials(p.constructionFloors);
        for (std::size_t m = 0; m < processedMaterialCount(); ++m)
            optionalNumber(p.materialAllowances ? std::optional<double>{p.materialAllowances->amount[m]}
                                                : std::nullopt);
    }
    void done() {
        if (next != count + 1)
            throw std::logic_error("Site persistence binding count mismatch");
        statement.execute();
        statement.reset();
        statement.clearBindings();
        next = 1;
    }
};
struct Reader {
    Statement& statement;
    int next = 0;
    std::int64_t i() {
        return statement.columnInt64Strict(next++);
    }
    int integer() {
        const auto v = i();
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
            throw std::runtime_error("Site integer outside supported range");
        return static_cast<int>(v);
    }
    std::size_t size() {
        const auto v = i();
        if (v < 0 || static_cast<std::uint64_t>(v) > std::numeric_limits<std::size_t>::max())
            throw std::runtime_error("Site count is invalid");
        return static_cast<std::size_t>(v);
    }
    bool boolean() {
        const auto v = i();
        if (v != 0 && v != 1)
            throw std::runtime_error("Site boolean is invalid");
        return v != 0;
    }
    double n() {
        return statement.columnDoubleStrict(next++);
    }
    std::string t() {
        return statement.columnText(next++);
    }
    template <class Id> Id id() {
        return Id{i()};
    }
    template <class Enum> Enum e() {
        return static_cast<Enum>(integer());
    }
    template <class Id> std::optional<Id> optionalId() {
        if (statement.columnIsNull(next)) {
            ++next;
            return std::nullopt;
        }
        return id<Id>();
    }
    std::optional<std::int64_t> optionalDay() {
        if (statement.columnIsNull(next)) {
            ++next;
            return std::nullopt;
        }
        return i();
    }
    std::optional<double> optionalNumber() {
        if (statement.columnIsNull(next)) {
            ++next;
            return std::nullopt;
        }
        return n();
    }
    ProcessedMaterialSet materials() {
        ProcessedMaterialSet v;
        for (double& amount : v.amount)
            amount = n();
        return v;
    }
    SiteOperatingPolicy operating() {
        SiteOperatingPolicy p;
        p.leaderId = optionalId<PersonId>();
        p.enabled = boolean();
        p.requestedIcePerDay = n();
        p.reactorFuelFloor = n();
        p.compositesFloor = n();
        p.lifetimeDutyAllowance = optionalNumber();
        return p;
    }
    SiteDevelopmentPolicy policy() {
        SiteDevelopmentPolicy p;
        p.homePropellantFloor = n();
        p.additionalPropellantAllowance = optionalNumber();
        p.returnContingencyFraction = n();
        p.constructionFloors = materials();
        ProcessedMaterialSet values;
        std::size_t present = 0;
        for (double& amount : values.amount) {
            const auto value = optionalNumber();
            if (value) {
                amount = *value;
                ++present;
            }
        }
        if (present != 0 && present != processedMaterialCount())
            throw std::runtime_error("Site material allowance channels are incomplete");
        if (present)
            p.materialAllowances = values;
        return p;
    }
    void ordinal(std::size_t expected) {
        if (i() != static_cast<std::int64_t>(expected))
            throw std::runtime_error("Site ordered child has duplicate, missing, or invalid ordinal");
    }
};
template <class T, class Id> T& parent(std::vector<T>& rows, Id id) {
    const auto it = std::find_if(rows.begin(), rows.end(), [=](const T& value) { return value.id == id; });
    if (it == rows.end())
        throw std::runtime_error("Site child references missing parent");
    return *it;
}
constexpr int materialColumns = static_cast<int>(processedMaterialCount());
constexpr int policyColumns = 3 + 2 * materialColumns;
} // namespace

void clearSiteState(Database& db) {
    db.execute(R"sql(
DELETE FROM site_operating_reports;
DELETE FROM site_extraction_receipts;
DELETE FROM site_duty_receipts;
DELETE FROM site_development_reports;
DELETE FROM site_development_work;
DELETE FROM site_installed_modules;
DELETE FROM site_development_rows;
DELETE FROM site_development_programs;
DELETE FROM resource_sites;
DELETE FROM colony_processing_totals;
DELETE FROM site_construction_binding;
DELETE FROM site_module_catalog;
)sql");
}

void saveSiteState(Database& db, const GameState& state) {
    Writer definitions(db, "site_module_catalog", 12 + materialColumns);
    for (std::size_t ordinal = 0; ordinal < state.siteModuleCatalog.size(); ++ordinal) {
        const auto& d = state.siteModuleCatalog[ordinal];
        definitions.i(static_cast<std::int64_t>(ordinal));
        definitions.e(d.kind);
        definitions.i(d.version);
        definitions.n(d.assemblyWorkdays);
        definitions.n(d.powerGeneration);
        definitions.n(d.powerDemand);
        definitions.n(d.extractionPerDay);
        definitions.n(d.handlingPerDay);
        definitions.n(d.rawStorage);
        definitions.n(d.supportedExtractors);
        definitions.n(d.reactorFuelPerDuty);
        definitions.n(d.compositesPerDuty);
        definitions.materials(d.cost);
        definitions.done();
    }
    Writer binding(db, "site_construction_binding", 2);
    binding.i(1);
    binding.optionalId(state.siteConstructionFamilyId);
    binding.done();
    Writer totals(db, "colony_processing_totals", 3);
    for (const auto& colony : state.colonies)
        for (std::size_t m = 0; m < processedMaterialCount(); ++m) {
            totals.id(colony.id);
            totals.i(static_cast<std::int64_t>(m));
            totals.n(colony.processedProductionTotals.amount[m]);
            totals.done();
        }
    Writer sites(db, "resource_sites", 19 + materialColumns + static_cast<int>(mineralCount()));
    for (std::size_t ordinal = 0; ordinal < state.resourceSites.size(); ++ordinal) {
        const auto& s = state.resourceSites[ordinal];
        sites.id(s.id);
        sites.i(static_cast<std::int64_t>(ordinal));
        sites.id(s.bodyId);
        sites.t(s.name);
        sites.i(s.createdDay);
        sites.optionalId(s.institutionId);
        sites.i(s.operatingRevision);
        sites.i(s.reportStartDay);
        sites.i(s.nextReportDay);
        sites.e(s.issue.cause);
        sites.i(s.issue.episodeStartedDay);
        sites.t(s.issue.message);
        sites.i(s.issue.acknowledged);
        sites.operating(s.operatingPolicy);
        sites.materials(s.processedStock);
        for (double amount : s.rawStock.amount)
            sites.n(amount);
        sites.done();
    }
    Writer programs(db, "site_development_programs", 30 + policyColumns);
    Writer rows(db, "site_development_rows", 6 + materialColumns);
    Writer work(db, "site_development_work", 11 + materialColumns);
    Writer reports(db, "site_development_reports", 16 + policyColumns + materialColumns);
    for (std::size_t ordinal = 0; ordinal < state.siteDevelopmentPrograms.size(); ++ordinal) {
        const auto& p = state.siteDevelopmentPrograms[ordinal];
        const auto& a = p.charter.assignments;
        programs.id(p.id);
        programs.i(static_cast<std::int64_t>(ordinal));
        programs.id(p.charter.siteId);
        programs.id(p.charter.supportColonyId);
        programs.i(p.charter.catalogVersion);
        programs.t(a.name);
        programs.optionalId(a.builderId);
        programs.optionalId(a.teamId);
        programs.optionalId(a.leaderId);
        programs.i(p.createdDay);
        programs.i(p.charterRevision);
        programs.e(p.lifecycle);
        programs.e(p.closure);
        programs.optionalDay(p.closedDay);
        programs.optionalId(p.leasedBuilderId);
        programs.optionalId(p.leasedTeamId);
        programs.i(p.holdsSiteConstruction);
        programs.e(p.task);
        programs.optionalId(p.taskBuilderId);
        programs.optionalId(p.taskTeamId);
        programs.n(p.commissioningWork);
        programs.optionalId(p.commissioningWorkshopId);
        programs.optionalDay(p.commissionedDay);
        programs.n(p.fuelLoaded);
        programs.n(p.fuelBurned);
        programs.i(p.reportStartDay);
        programs.i(p.nextReportDay);
        programs.t(p.issue.signature);
        programs.t(p.issue.message);
        programs.i(p.issue.acknowledged);
        programs.policy(a.policy);
        programs.done();
        for (std::size_t n = 0; n < p.charter.package.size(); ++n) {
            const auto& row = p.rows.at(n);
            rows.id(p.id);
            rows.i(static_cast<std::int64_t>(n));
            rows.e(p.charter.package[n].kind);
            rows.i(p.charter.package[n].quantity);
            rows.n(row.workCompleted);
            rows.optionalId(row.workshopShipId);
            rows.materials(row.consumed);
            rows.done();
        }
        for (std::size_t n = 0; n < p.workReceipts.size(); ++n) {
            const auto& r = p.workReceipts[n];
            work.id(p.id);
            work.i(static_cast<std::int64_t>(n));
            work.i(r.day);
            work.i(r.charterRevision);
            work.e(r.kind);
            work.i(r.packageRow);
            work.id(r.builderId);
            work.id(r.workshopShipId);
            work.id(r.teamId);
            work.id(r.leaderId);
            work.n(r.work);
            work.materials(r.consumed);
            work.done();
        }
        for (std::size_t n = 0; n < p.reports.size(); ++n) {
            const auto& r = p.reports[n];
            reports.id(p.id);
            reports.i(static_cast<std::int64_t>(n));
            reports.i(r.startDay);
            reports.i(r.endDay);
            reports.i(r.isNinetyDayReview);
            reports.i(r.charterRevision);
            reports.policy(r.policy);
            reports.n(r.assemblyWork);
            reports.n(r.commissioningWork);
            reports.materials(r.consumed);
            reports.n(r.fuelLoaded);
            reports.n(r.fuelBurned);
            reports.optionalId(r.builderId);
            reports.optionalId(r.teamId);
            reports.optionalId(r.bodyId);
            reports.i(r.commissioned);
            reports.t(r.waitingReason);
            reports.i(r.auditThroughId);
            reports.done();
        }
    }
    Writer installed(db, "site_installed_modules", 8), duty(db, "site_duty_receipts", 16),
        extraction(db, "site_extraction_receipts", 11), operatingReports(db, "site_operating_reports", 24);
    for (const auto& s : state.resourceSites) {
        for (std::size_t n = 0; n < s.installed.size(); ++n) {
            const auto& g = s.installed[n];
            installed.id(s.id);
            installed.i(static_cast<std::int64_t>(n));
            installed.id(g.programId);
            installed.i(g.packageRow);
            installed.i(g.catalogVersion);
            installed.e(g.kind);
            installed.i(g.quantity);
            installed.i(g.commissionedDay);
            installed.done();
        }
        for (std::size_t n = 0; n < s.dutyReceipts.size(); ++n) {
            const auto& r = s.dutyReceipts[n];
            duty.id(s.id);
            duty.i(static_cast<std::int64_t>(n));
            duty.i(r.day);
            duty.i(r.operatingRevision);
            duty.id(r.leaderId);
            duty.i(static_cast<std::int64_t>(r.installedGroupCutoff));
            duty.n(r.duty);
            duty.n(r.reactorFuel);
            duty.n(r.composites);
            duty.n(r.rawHandling);
            duty.operating(r.policy);
            duty.done();
        }
        for (std::size_t n = 0; n < s.extractionReceipts.size(); ++n) {
            const auto& r = s.extractionReceipts[n];
            extraction.id(s.id);
            extraction.i(static_cast<std::int64_t>(n));
            extraction.i(r.day);
            extraction.i(r.operatingRevision);
            extraction.id(r.leaderId);
            extraction.i(static_cast<std::int64_t>(r.installedGroupCutoff));
            extraction.n(r.nominalAttempt);
            extraction.n(r.recoveredIce);
            extraction.n(r.freeRawRoom);
            extraction.n(r.availableHandling);
            extraction.t(r.observation);
            extraction.done();
        }
        for (std::size_t n = 0; n < s.reports.size(); ++n) {
            const auto& r = s.reports[n];
            operatingReports.id(s.id);
            operatingReports.i(static_cast<std::int64_t>(n));
            operatingReports.i(r.startDay);
            operatingReports.i(r.endDay);
            operatingReports.i(r.isNinetyDayReview);
            operatingReports.i(r.operatingRevision);
            operatingReports.operating(r.policy);
            operatingReports.i(static_cast<std::int64_t>(r.installedGroupCutoff));
            operatingReports.n(r.supportedDuty);
            operatingReports.n(r.reactorFuel);
            operatingReports.n(r.composites);
            operatingReports.i(r.attempts);
            operatingReports.n(r.recoveredIce);
            operatingReports.n(r.rawOccupancy);
            operatingReports.n(r.rawCapacity);
            operatingReports.n(r.exportedIce);
            operatingReports.n(r.deliveredIce);
            operatingReports.t(r.waitingReason);
            operatingReports.i(r.auditThroughId);
            operatingReports.done();
        }
    }
}

void loadSiteState(Database& db, GameState& state) {
    Statement definitions(db, "SELECT * FROM site_module_catalog ORDER BY ordinal;");
    while (definitions.step()) {
        Reader r{definitions};
        r.ordinal(state.siteModuleCatalog.size());
        SiteModuleDefinition d;
        d.kind = r.e<SiteModuleKind>();
        d.version = r.integer();
        d.assemblyWorkdays = r.n();
        d.powerGeneration = r.n();
        d.powerDemand = r.n();
        d.extractionPerDay = r.n();
        d.handlingPerDay = r.n();
        d.rawStorage = r.n();
        d.supportedExtractors = r.n();
        d.reactorFuelPerDuty = r.n();
        d.compositesPerDuty = r.n();
        d.cost = r.materials();
        state.siteModuleCatalog.push_back(d);
    }
    Statement binding(db, "SELECT singleton,family_id FROM site_construction_binding;");
    if (!binding.step() || binding.columnInt64Strict(0) != 1)
        throw std::runtime_error("Missing site construction family binding");
    Reader br{binding, 1};
    state.siteConstructionFamilyId = br.optionalId<EquipmentFamilyId>();
    if (binding.step())
        throw std::runtime_error("Duplicate site construction binding");
    Statement totals(
        db, "SELECT colony_id,material,amount FROM colony_processing_totals ORDER BY colony_id,material;");
    std::vector<std::pair<ColonyId, std::size_t>> counts;
    while (totals.step()) {
        Reader r{totals};
        auto& colony = parent(state.colonies, r.id<ColonyId>());
        const auto material = r.size();
        auto it =
            std::find_if(counts.begin(), counts.end(), [&](const auto& x) { return x.first == colony.id; });
        if (it == counts.end()) {
            counts.emplace_back(colony.id, 0);
            it = counts.end() - 1;
        }
        if (material != it->second++ || material >= processedMaterialCount())
            throw std::runtime_error("Incomplete or invalid production total channels");
        colony.processedProductionTotals.amount[material] = r.n();
    }
    if (counts.size() != state.colonies.size() ||
        std::any_of(counts.begin(), counts.end(),
                    [](const auto& c) { return c.second != processedMaterialCount(); }))
        throw std::runtime_error("Missing cumulative colony production totals");
    Statement sites(db, "SELECT * FROM resource_sites ORDER BY ordinal;");
    while (sites.step()) {
        Reader r{sites};
        ResourceSite s;
        s.id = r.id<SiteId>();
        r.ordinal(state.resourceSites.size());
        s.bodyId = r.id<BodyId>();
        s.name = r.t();
        s.createdDay = r.i();
        s.institutionId = r.optionalId<InstitutionId>();
        s.operatingRevision = r.integer();
        s.reportStartDay = r.i();
        s.nextReportDay = r.i();
        s.issue.cause = r.e<SiteOperatingIssueCause>();
        s.issue.episodeStartedDay = r.i();
        s.issue.message = r.t();
        s.issue.acknowledged = r.boolean();
        s.operatingPolicy = r.operating();
        s.processedStock = r.materials();
        for (double& amount : s.rawStock.amount)
            amount = r.n();
        state.resourceSites.push_back(std::move(s));
    }
    Statement programs(db, "SELECT * FROM site_development_programs ORDER BY ordinal;");
    while (programs.step()) {
        Reader r{programs};
        SiteDevelopmentProgram p;
        p.id = r.id<SiteDevelopmentProgramId>();
        r.ordinal(state.siteDevelopmentPrograms.size());
        p.charter.siteId = r.id<SiteId>();
        p.charter.supportColonyId = r.id<ColonyId>();
        p.charter.catalogVersion = r.integer();
        auto& a = p.charter.assignments;
        a.name = r.t();
        a.builderId = r.optionalId<FleetId>();
        a.teamId = r.optionalId<MaintenanceTeamId>();
        a.leaderId = r.optionalId<PersonId>();
        p.createdDay = r.i();
        p.charterRevision = r.integer();
        p.lifecycle = r.e<SiteDevelopmentLifecycle>();
        p.closure = r.e<SiteDevelopmentClosure>();
        p.closedDay = r.optionalDay();
        p.leasedBuilderId = r.optionalId<FleetId>();
        p.leasedTeamId = r.optionalId<MaintenanceTeamId>();
        p.holdsSiteConstruction = r.boolean();
        p.task = r.e<SiteDevelopmentTask>();
        p.taskBuilderId = r.optionalId<FleetId>();
        p.taskTeamId = r.optionalId<MaintenanceTeamId>();
        p.commissioningWork = r.n();
        p.commissioningWorkshopId = r.optionalId<ShipId>();
        p.commissionedDay = r.optionalDay();
        p.fuelLoaded = r.n();
        p.fuelBurned = r.n();
        p.reportStartDay = r.i();
        p.nextReportDay = r.i();
        p.issue.signature = r.t();
        p.issue.message = r.t();
        p.issue.acknowledged = r.boolean();
        a.policy = r.policy();
        state.siteDevelopmentPrograms.push_back(std::move(p));
    }
    Statement rows(db, "SELECT * FROM site_development_rows ORDER BY program_id,ordinal;");
    while (rows.step()) {
        Reader r{rows};
        auto& p = parent(state.siteDevelopmentPrograms, r.id<SiteDevelopmentProgramId>());
        r.ordinal(p.rows.size());
        SiteModuleInstall install;
        install.kind = r.e<SiteModuleKind>();
        install.quantity = r.integer();
        SiteAssemblyRow row;
        row.workCompleted = r.n();
        row.workshopShipId = r.optionalId<ShipId>();
        row.consumed = r.materials();
        p.charter.package.push_back(install);
        p.rows.push_back(row);
    }
    Statement work(db, "SELECT * FROM site_development_work ORDER BY program_id,ordinal;");
    while (work.step()) {
        Reader r{work};
        auto& p = parent(state.siteDevelopmentPrograms, r.id<SiteDevelopmentProgramId>());
        r.ordinal(p.workReceipts.size());
        SiteDevelopmentWorkReceipt w;
        w.day = r.i();
        w.charterRevision = r.integer();
        w.kind = r.e<SiteDevelopmentWorkKind>();
        w.packageRow = r.integer();
        w.builderId = r.id<FleetId>();
        w.workshopShipId = r.id<ShipId>();
        w.teamId = r.id<MaintenanceTeamId>();
        w.leaderId = r.id<PersonId>();
        w.work = r.n();
        w.consumed = r.materials();
        p.workReceipts.push_back(w);
    }
    Statement reports(db, "SELECT * FROM site_development_reports ORDER BY program_id,ordinal;");
    while (reports.step()) {
        Reader r{reports};
        auto& p = parent(state.siteDevelopmentPrograms, r.id<SiteDevelopmentProgramId>());
        r.ordinal(p.reports.size());
        SiteDevelopmentReport x;
        x.startDay = r.i();
        x.endDay = r.i();
        x.isNinetyDayReview = r.boolean();
        x.charterRevision = r.integer();
        x.policy = r.policy();
        x.assemblyWork = r.n();
        x.commissioningWork = r.n();
        x.consumed = r.materials();
        x.fuelLoaded = r.n();
        x.fuelBurned = r.n();
        x.builderId = r.optionalId<FleetId>();
        x.teamId = r.optionalId<MaintenanceTeamId>();
        x.bodyId = r.optionalId<BodyId>();
        x.commissioned = r.boolean();
        x.waitingReason = r.t();
        x.auditThroughId = r.i();
        p.reports.push_back(x);
    }
    Statement installed(db, "SELECT * FROM site_installed_modules ORDER BY site_id,ordinal;");
    while (installed.step()) {
        Reader r{installed};
        auto& s = parent(state.resourceSites, r.id<SiteId>());
        r.ordinal(s.installed.size());
        InstalledSiteModule g;
        g.programId = r.id<SiteDevelopmentProgramId>();
        g.packageRow = r.integer();
        g.catalogVersion = r.integer();
        g.kind = r.e<SiteModuleKind>();
        g.quantity = r.integer();
        g.commissionedDay = r.i();
        s.installed.push_back(g);
    }
    Statement duty(db, "SELECT * FROM site_duty_receipts ORDER BY site_id,ordinal;");
    while (duty.step()) {
        Reader r{duty};
        auto& s = parent(state.resourceSites, r.id<SiteId>());
        r.ordinal(s.dutyReceipts.size());
        SiteDutyReceipt x;
        x.day = r.i();
        x.operatingRevision = r.integer();
        x.leaderId = r.id<PersonId>();
        x.installedGroupCutoff = r.size();
        x.duty = r.n();
        x.reactorFuel = r.n();
        x.composites = r.n();
        x.rawHandling = r.n();
        x.policy = r.operating();
        s.dutyReceipts.push_back(x);
    }
    Statement extraction(db, "SELECT * FROM site_extraction_receipts ORDER BY site_id,ordinal;");
    while (extraction.step()) {
        Reader r{extraction};
        auto& s = parent(state.resourceSites, r.id<SiteId>());
        r.ordinal(s.extractionReceipts.size());
        SiteExtractionReceipt x;
        x.day = r.i();
        x.operatingRevision = r.integer();
        x.leaderId = r.id<PersonId>();
        x.installedGroupCutoff = r.size();
        x.nominalAttempt = r.n();
        x.recoveredIce = r.n();
        x.freeRawRoom = r.n();
        x.availableHandling = r.n();
        x.observation = r.t();
        s.extractionReceipts.push_back(x);
    }
    Statement operatingReports(db, "SELECT * FROM site_operating_reports ORDER BY site_id,ordinal;");
    while (operatingReports.step()) {
        Reader r{operatingReports};
        auto& s = parent(state.resourceSites, r.id<SiteId>());
        r.ordinal(s.reports.size());
        SiteOperatingReport x;
        x.startDay = r.i();
        x.endDay = r.i();
        x.isNinetyDayReview = r.boolean();
        x.operatingRevision = r.integer();
        x.policy = r.operating();
        x.installedGroupCutoff = r.size();
        x.supportedDuty = r.n();
        x.reactorFuel = r.n();
        x.composites = r.n();
        x.attempts = r.integer();
        x.recoveredIce = r.n();
        x.rawOccupancy = r.n();
        x.rawCapacity = r.n();
        x.exportedIce = r.n();
        x.deliveredIce = r.n();
        x.waitingReason = r.t();
        x.auditThroughId = r.i();
        s.reports.push_back(x);
    }
}
} // namespace deep::save
