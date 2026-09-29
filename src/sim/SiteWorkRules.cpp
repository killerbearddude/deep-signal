// Conservative proportional work accounting for P4B. Readiness is typed;
// English explanations never decide whether physical work can be performed.
#include "sim/SiteWorkRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace deep {
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
bool nonnegative(double value) {
    return std::isfinite(value) && value >= 0;
}
void amounts(const ProcessedMaterialSet& values) {
    for (double value : values.amount)
        require(nonnegative(value), "Invalid site stock, cost or authority");
}
SiteWorkReadiness wait(SiteWorkCause cause, std::string message) {
    return {false, cause, std::move(message), 0, {}};
}
bool representableDebit(double before, double amount) {
    const double after = before - amount;
    if (!nonnegative(after))
        return false;
    if (amount == 0)
        return true;
    const double actual = before - after;
    // Relative-only comparison cannot waive a tiny genuinely missing input.
    return actual > 0 && std::abs(actual - amount) <= 1e-10 * std::max(actual, amount);
}
bool representableCredit(double before, double amount) {
    const double after = before + amount;
    const double actual = after - before;
    return std::isfinite(after) &&
           (amount == 0 || (actual > 0 && std::abs(actual - amount) <= 1e-10 * std::max(actual, amount)));
}
} // namespace
SiteWorkReadiness prepareSiteAssembly(const SiteAssemblyInputs& in) {
    require(nonnegative(in.requiredWork) && in.requiredWork > 0 && nonnegative(in.completedWork) &&
                in.completedWork <= in.requiredWork && nonnegative(in.teamRate) &&
                nonnegative(in.workshopRate),
            "Invalid site assembly work or rates");
    amounts(in.totalCost);
    amounts(in.currentStock);
    amounts(in.openingStock);
    amounts(in.floors);
    amounts(in.lifetimeConsumed);
    if (in.lifetimeAllowances)
        amounts(*in.lifetimeAllowances);
    if (in.completedWork == in.requiredWork)
        return wait(SiteWorkCause::Complete, "Assembly row is complete");
    if (in.teamRate == 0)
        return wait(SiteWorkCause::NoTeamRate, "Waiting for positive engineering-team throughput");
    if (in.workshopRate == 0)
        return wait(SiteWorkCause::NoWorkshopRate, "Waiting for compatible powered workshop throughput");
    double work = std::min({in.requiredWork - in.completedWork, in.teamRate, in.workshopRate});
    ProcessedMaterialSet perWork, available;
    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        const double cost = in.totalCost.amount[i];
        if (cost == 0)
            continue;
        const double rate = cost / in.requiredWork;
        if (!std::isfinite(rate) || rate <= 0)
            return wait(SiteWorkCause::NumericLimit, "Assembly material/work ratio is not representable");
        perWork.amount[i] = rate;
        const double eligible = std::min(in.currentStock.amount[i], in.openingStock.amount[i]);
        available.amount[i] = std::max(0.0, eligible - in.floors.amount[i]);
        if (available.amount[i] == 0) {
            const bool protectedStock = eligible > 0 && in.floors.amount[i] > 0;
            return wait(protectedStock ? SiteWorkCause::MaterialFloor : SiteWorkCause::MaterialStock,
                        protectedStock ? "Waiting: required construction material is protected by a floor"
                                       : "Waiting for opening construction material stock");
        }
        work = std::min(work, available.amount[i] / rate);
        if (in.lifetimeAllowances) {
            const double left =
                std::max(0.0, in.lifetimeAllowances->amount[i] - in.lifetimeConsumed.amount[i]);
            if (left == 0)
                return wait(SiteWorkCause::MaterialAllowance,
                            "Waiting: construction-material allowance exhausted");
            work = std::min(work, left / rate);
        }
    }
    const double remainingWork = in.requiredWork - in.completedWork;
    // A shared material pool accumulates rounding across earlier row debits.
    // Bound final-step normalization to 32 ULPs of the positive local delta;
    // every required material still needs a positive affordable real debit.
    // This never turns a zero-stock result into free work.
    const double rounding = 32 * std::numeric_limits<double>::epsilon();
    if (work > 0 && remainingWork - work <= rounding * std::max(work, remainingWork))
        work = remainingWork;
    if (!std::isfinite(work) || work <= 0 || !representableCredit(in.completedWork, work))
        return wait(SiteWorkCause::NumericLimit, "Positive assembly progress is not representable");
    SiteWorkReadiness result{true, SiteWorkCause::Ready, "Ready for proportional field assembly", work, {}};
    for (std::size_t i = 0; i < processedMaterialCount(); ++i) {
        double debit = work * perWork.amount[i];
        double limit = available.amount[i];
        if (in.lifetimeAllowances)
            limit = std::min(limit,
                             std::max(0.0, in.lifetimeAllowances->amount[i] - in.lifetimeConsumed.amount[i]));
        if (limit > 0 && debit > limit && debit - limit <= rounding * std::max(debit, limit))
            debit = limit;
        if (!nonnegative(debit) || (perWork.amount[i] > 0 && debit == 0) || debit > available.amount[i] ||
            !representableDebit(in.currentStock.amount[i], debit) ||
            !representableDebit(in.openingStock.amount[i], debit) ||
            !representableCredit(in.lifetimeConsumed.amount[i], debit) ||
            (in.lifetimeAllowances &&
             debit > std::max(0.0, in.lifetimeAllowances->amount[i] - in.lifetimeConsumed.amount[i])))
            return wait(SiteWorkCause::NumericLimit,
                        "Assembly cannot conserve a representable material debit");
        result.consumed.amount[i] = debit;
    }
    return result;
}
SiteWorkReadiness prepareSiteDuty(const SiteDutyInputs& in) {
    amounts(in.currentStock);
    amounts(in.openingStock);
    require(nonnegative(in.reactorFuelFloor) && nonnegative(in.compositesFloor) &&
                nonnegative(in.dutySpent) &&
                (!in.lifetimeDutyAllowance || nonnegative(*in.lifetimeDutyAllowance)),
            "Invalid site operating authority");
    const auto& installed = in.installed;
    for (double value : {installed.powerGeneration, installed.powerDemand, installed.reactorFuelPerDuty,
                         installed.compositesPerDuty})
        require(nonnegative(value), "Invalid commissioned operating capability");
    if (!in.enabled)
        return wait(SiteWorkCause::Disabled, "Site operation suspended");
    if (!in.hasLeader)
        return wait(SiteWorkCause::NoLeader, "Waiting for an operating leader");
    if (installed.counts[static_cast<std::size_t>(SiteModuleKind::Power)] <= 0 ||
        installed.reactorFuelPerDuty <= 0 || installed.powerGeneration <= 0)
        return wait(SiteWorkCause::NoPower, "Waiting for commissioned site power equipment");
    if (installed.counts[static_cast<std::size_t>(SiteModuleKind::AutomationSupport)] <= 0 ||
        installed.compositesPerDuty <= 0)
        return wait(SiteWorkCause::NoAutomation, "Waiting for commissioned automation/support equipment");
    if (installed.powerGeneration < installed.powerDemand)
        return wait(SiteWorkCause::PowerDeficit,
                    "Waiting: installed all-active power demand exceeds generation");
    const double fuel = std::max(0.0, std::min(in.currentStock.get(ProcessedMaterial::ReactorFuel),
                                               in.openingStock.get(ProcessedMaterial::ReactorFuel)) -
                                          in.reactorFuelFloor);
    const double composites =
        std::max(0.0, std::min(in.currentStock.get(ProcessedMaterial::IndustrialComposites),
                               in.openingStock.get(ProcessedMaterial::IndustrialComposites)) -
                          in.compositesFloor);
    const double authority =
        in.lifetimeDutyAllowance ? std::max(0.0, *in.lifetimeDutyAllowance - in.dutySpent) : 1.0;
    if (authority == 0)
        return wait(SiteWorkCause::DutyAllowance, "Waiting: supported-duty allowance exhausted");
    if (fuel == 0)
        return wait(SiteWorkCause::ReactorFuel, "Waiting for opening ReactorFuel above its floor");
    if (composites == 0)
        return wait(SiteWorkCause::Composites, "Waiting for opening IndustrialComposites above its floor");
    const double duty = std::min(
        {1.0, authority, fuel / installed.reactorFuelPerDuty, composites / installed.compositesPerDuty});
    if (!std::isfinite(duty) || duty <= 0 || !representableCredit(in.dutySpent, duty))
        return wait(SiteWorkCause::NumericLimit, "Positive supported site duty is not representable");
    SiteWorkReadiness result{true, SiteWorkCause::Ready, "Ready for supported site operation", duty, {}};
    result.consumed.set(ProcessedMaterial::ReactorFuel, duty * installed.reactorFuelPerDuty);
    result.consumed.set(ProcessedMaterial::IndustrialComposites, duty * installed.compositesPerDuty);
    for (auto material : {ProcessedMaterial::ReactorFuel, ProcessedMaterial::IndustrialComposites}) {
        const double debit = result.consumed.get(material);
        if (!nonnegative(debit) || debit == 0 || !representableDebit(in.currentStock.get(material), debit) ||
            !representableDebit(in.openingStock.get(material), debit))
            return wait(SiteWorkCause::NumericLimit, "Site support debit is not representable");
    }
    return result;
}
} // namespace deep
