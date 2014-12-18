// Reaktor is a C++ library for computational reaction modelling.
//
// Copyright (C) 2014 Allan Leal
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#pragma once

// C++ includes
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Reaktor includes
#include <Reaktor/Common/Index.hpp>
#include <Reaktor/Common/Matrix.hpp>
#include <Reaktor/Common/ThermoScalar.hpp>

namespace Reaktor {

// Forward declarations
struct ReactionKineticsModel;
struct ReactionThermoModel;
 class ChemicalScalar;
 class ChemicalVector;

/// Provide a computational representation of a chemical reaction.
/// The Reaction class provides a representation of a chemical reaction
/// and operations such as the calculation of equilibrium constants at
/// given temperature and pressure points, reaction quotients, and
/// reaction rates.
/// @see ReactionRate, EquilibriumConstant
/// @ingroup Core
class Reaction
{
public:
    /// Construct a default Reaction instance
    Reaction();

    /// Construct a copy of a Reaction instance
    Reaction(const Reaction& other);

    /// Destroy this instance
    virtual ~Reaction();

    /// Assign a Reaction instance to this instance
    auto operator=(Reaction other) -> Reaction&;

    /// Set the names of the reacting species of the reation
    auto setSpecies(const std::vector<std::string>& species) -> Reaction&;

    /// Set the indices of the reacting species of the reation
    auto setIndices(const Indices& indices) -> Reaction&;

    /// Set the stoichiometries of the reacting species of the reation
    auto setStoichiometries(const std::vector<double>& stoichiometries) -> Reaction&;

    /// Set the thermodynamic model of the reaction
    auto setThermoModel(const ReactionThermoModel& thermo_model) -> Reaction&;

    /// Set the kinetics model of the reaction
    auto setKineticsModel(const ReactionKineticsModel& kinetics_model) -> Reaction&;

    /// Get the indices of the reacting species of the reaction
    auto species() const -> const std::vector<std::string>&;

    /// Get the indices of the reacting species of the reaction
    auto indices() const -> const Indices&;

    /// Get the stoichiometries of the reacting species of the reaction
    auto stoichiometries() const -> const std::vector<double>&;

    /// Get the thermodynamic model of the reaction
    auto thermoModel() const -> const ReactionThermoModel&;

    /// Get the kinetics model of the reaction
    auto kineticsModel() const -> const ReactionKineticsModel&;

private:
    struct Impl;

    std::unique_ptr<Impl> pimpl;
};

/// Define the function signature of the rate of a reaction (in units of mol/s).
/// @param T The temperature value (in units of K)
/// @param P The pressure value (in units of Pa)
/// @param n The molar amounts of all species in the system (in units of mol)
/// @param a The activities of all species in the system and their molar derivatives
/// @return The rate of the reaction and its molar derivatives (in units of mol/s)
/// @see Reaction
/// @ingroup Core
typedef std::function<
    ChemicalScalar(double T, double P, const Vector& n, const ChemicalVector& a)>
        ReactionRateFunction;

/// A type to describe the thermodynamic model of a reaction
/// @ingroup Core
struct ReactionThermoModel
{
    /// The function for the equilibrium constant of the reaction (in terms of its natural logarithm)
    ThermoScalarFunction lnk;

    /// The function for the standard molar Gibbs free energy of the reaction  (in units of J/mol).
    ThermoScalarFunction gibbs_energy;

    /// The function for the standard molar Helmholtz free energy of the reaction  (in units of J/mol).
    ThermoScalarFunction helmholtz_energy;

    /// The function for the standard molar internal energy of the reaction  (in units of J/mol).
    ThermoScalarFunction internal_energy;

    /// The function for the standard molar enthalpy of the reaction  (in units of J/mol).
    ThermoScalarFunction enthalpy;

    /// The function for the standard molar entropy of the reaction (in units of J/K).
    ThermoScalarFunction entropy;
};

/// A type to describe the kinetics model of a reaction
/// @ingroup Core
struct ReactionKineticsModel
{
    /// The function for the kinetic rate of the reaction (in units of mol/s)
    ReactionRateFunction rate;
};

} // namespace Reaktor
