// Reaktoro is a C++ library for computational reaction modelling.
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

#include "PhreeqcDatabase.hpp"

// Reaktoro includes
#include <Reaktoro/Common/ConvertUtils.hpp>
#include <Reaktoro/Common/Exception.hpp>
#include <Reaktoro/Common/StringUtils.hpp>
#include <Reaktoro/Core/Element.hpp>
#include <Reaktoro/Thermodynamics/Core/Database.hpp>
#include <Reaktoro/Thermodynamics/Species/AqueousSpecies.hpp>
#include <Reaktoro/Thermodynamics/Species/GaseousSpecies.hpp>
#include <Reaktoro/Thermodynamics/Species/MineralSpecies.hpp>

// Phreeqc includes
#include <Reaktoro/Interfaces/PHREEQC.hpp>
#include <Reaktoro/Interfaces/PhreeqcUtils.hpp>

namespace Reaktoro {
namespace {

auto createElement(const PhreeqcElement* e) -> Element
{
	Element element;
	element.setName(e->name);
	element.setMolarMass(e->gfw);
	return element;
}

auto elementsInSpecies(const PhreeqcSpecies* s) -> std::map<Element, double>
{
	std::map<Element, double> res;
	for(auto pair : PhreeqcUtils::elements(s))
		res.insert({createElement(pair.first), pair.second});
	return res;
}

auto elementsInPhase(const PhreeqcPhase* p) -> std::map<Element, double>
{
	std::map<Element, double> res;
	for(auto pair : PhreeqcUtils::elements(p))
		res.insert({createElement(pair.first), pair.second});
	return res;
}

template<typename SpeciesType>
auto speciesThermoParamsPhreeqc(const SpeciesType& species) -> SpeciesThermoParamsPhreeqc
{
	SpeciesThermoParamsPhreeqc params;
	params.reaction = PhreeqcUtils::reactionEquation(species);
	params.log_k = species->logk[logK_T0];
	params.delta_h = species->logk[delta_h];
	params.analytic = {
		species->logk[T_A1],
		species->logk[T_A2],
		species->logk[T_A3],
		species->logk[T_A4],
		species->logk[T_A5],
		species->logk[T_A6]};
	return params;
}

auto aqueousSpeciesThermoData(const PhreeqcSpecies* species) -> AqueousSpeciesThermoData
{
	SpeciesThermoParamsPhreeqc params = speciesThermoParamsPhreeqc(species);

	AqueousSpeciesThermoData data;

	// Set the PHREEQC thermodynamic data if the species reaction information
	if(!params.reaction.empty())
	    data.phreeqc.set(params);

	// If not, set the zero HKF model to the species
	else data.hkf.set(AqueousSpeciesThermoParamsHKF());

	return data;
}

auto gaseousSpeciesThermoData(const PhreeqcPhase* phase) -> GaseousSpeciesThermoData
{
	SpeciesThermoParamsPhreeqc params = speciesThermoParamsPhreeqc(phase);
	GaseousSpeciesThermoData data;
	data.phreeqc.set(params);
	return data;
}

auto mineralSpeciesThermoData(const PhreeqcPhase* phase) -> MineralSpeciesThermoData
{
	const double T = 278.15;
	const double P = 1e5;
	const double molar_volume = convertCubicCentimeterToCubicMeter(phase->logk[vm0]);

	SpeciesThermoInterpolatedProperties props;
	props.volume = BilinearInterpolator({T}, {P}, {molar_volume});

	SpeciesThermoParamsPhreeqc params = speciesThermoParamsPhreeqc(phase);

	MineralSpeciesThermoData data;
	data.properties.set(props);
	data.phreeqc.set(params);

	return data;
}

auto createAqueousSpecies(const PhreeqcSpecies* s) -> AqueousSpecies
{
	AqueousSpecies species;
	species.setName(s->name);
	species.setCharge(s->z);
	species.setElements(elementsInSpecies(s));
	species.setThermoData(aqueousSpeciesThermoData(s));
	return species;
}

auto createGaseousSpecies(const PhreeqcPhase* p) -> GaseousSpecies
{
	GaseousSpecies species;
	species.setName(p->name);
	species.setElements(elementsInPhase(p));
	species.setThermoData(gaseousSpeciesThermoData(p));
	return species;
}

auto createMineralSpecies(const PhreeqcPhase* p) -> MineralSpecies
{
	MineralSpecies species;
	species.setName(p->name);
	species.setElements(elementsInPhase(p));
	species.setThermoData(mineralSpeciesThermoData(p));
	return species;
}

} // namespace

struct PhreeqcDatabase::Impl
{
	/// The PHREEQC instance
	PHREEQC phreeqc;

	/// The list of elements in the database
	std::vector<Element> elements;

	/// The list of aqueous species in the database
	std::vector<AqueousSpecies> aqueous_species;

	/// The list of gaseous species in the database
	std::vector<GaseousSpecies> gaseous_species;

	/// The list of mineral species in the database
	std::vector<MineralSpecies> mineral_species;

	/// The aqueous master species in the database
	std::set<std::string> master_species;

	/// The indices of the aqueous master species in the database
	Indices idx_master_species;

	/// The map from a master species to the product species composed by it
	std::vector<std::set<std::string>> from_master_to_product_species;

	auto load(std::string filename) -> void
	{
		// Clear current state
		elements.clear();
		aqueous_species.clear();
		gaseous_species.clear();
		mineral_species.clear();
		master_species.clear();
		idx_master_species.clear();
		from_master_to_product_species.clear();

		// Initialize the phreeqc instance
		int errors = phreeqc.do_initialize();
		Assert(errors == 0, "Could not initialize Phreeqc.",
			"Call to method `Phreeqc::do_initialize` failed.");

		// Initialize the phreeqc database
		std::istream* db_cookie = new std::ifstream(filename, std::ios_base::in);
		phreeqc.Get_phrq_io()->push_istream(db_cookie);
		errors = phreeqc.read_database();
		phreeqc.Get_phrq_io()->clear_istream();
		phreeqc.Get_phrq_io()->Set_error_ostream(&std::cerr);
		phreeqc.Get_phrq_io()->Set_output_ostream(&std::cout);
		Assert(errors == 0, "Could not load the Phreeqc database file `" + filename + "`.",
			"Ensure `" + filename + "` points to the right path to the database file.");

		// Initialize the set of master species
		for(int i = 0; i < phreeqc.count_master; ++i)
			master_species.insert(phreeqc.master[i]->s->name);

		// Initialize the indices of the aqueous species that are master species
		for(int i = 0; i < phreeqc.count_s; ++i)
			if(master_species.count(phreeqc.s[i]->name))
				idx_master_species.push_back(i);

		// Initialize the map from master species to the product species
		from_master_to_product_species.resize(master_species.size());

		for(unsigned i = 0; i < idx_master_species.size(); ++i)
		{
		    const Index ispecies = idx_master_species[i];
			const std::string master_name = phreeqc.s[ispecies]->name;

			// Loop over all Phreeqc species instances (aqueous species)
			for(int j = 0; j < phreeqc.count_s; ++j)
			{
				const std::string product_name = phreeqc.s[j]->name;
				const auto equation = PhreeqcUtils::reactionEquation(phreeqc.s[j]);
				for(auto pair : equation)
					if(pair.first == master_name)
						from_master_to_product_species[i].insert(product_name);
			}

			// Loop over all Phreeqc phase instances (gaseous or mineral species)
			for(int j = 0; j < phreeqc.count_phases; ++j)
			{
				const std::string product_name = phreeqc.phases[j]->name;
				const auto equation = PhreeqcUtils::reactionEquation(phreeqc.phases[j]);
				for(auto pair : equation)
					if(pair.first == master_name)
						from_master_to_product_species[i].insert(product_name);
			}
		}

		// Initialize the elements
		for(int i = 0; i < phreeqc.count_elements; ++i)
			elements.push_back(createElement(phreeqc.elements[i]));

		// Initialize the aqueous species
		for(int i = 0; i < phreeqc.count_s; ++i)
			aqueous_species.push_back(createAqueousSpecies(phreeqc.s[i]));

		// Initialize the gaseous and mineral species
		for(int i = 0; i < phreeqc.count_phases; ++i)
			if(PhreeqcUtils::isGaseousSpecies(phreeqc.phases[i]))
				gaseous_species.push_back(createGaseousSpecies(phreeqc.phases[i]));
			else
				mineral_species.push_back(createMineralSpecies(phreeqc.phases[i]));
	}
};

PhreeqcDatabase::PhreeqcDatabase()
: pimpl(new Impl())
{}

PhreeqcDatabase::PhreeqcDatabase(std::string filename)
: PhreeqcDatabase()
{
	load(filename);
}

auto PhreeqcDatabase::load(std::string filename) -> void
{
	pimpl->load(filename);
}

auto PhreeqcDatabase::numElements() const -> unsigned
{
	return pimpl->elements.size();
}

auto PhreeqcDatabase::numAqueousSpecies() const -> unsigned
{
	return pimpl->aqueous_species.size();
}

auto PhreeqcDatabase::numGaseousSpecies() const -> unsigned
{
	return pimpl->gaseous_species.size();
}

auto PhreeqcDatabase::numMineralSpecies() const -> unsigned
{
	return pimpl->mineral_species.size();
}

auto PhreeqcDatabase::element(Index index) const -> Element
{
	return pimpl->elements[index];
}

auto PhreeqcDatabase::elements() const -> const std::vector<Element>&
{
	return pimpl->elements;
}

auto PhreeqcDatabase::aqueousSpecies(Index index) const -> AqueousSpecies
{
	return pimpl->aqueous_species[index];
}

auto PhreeqcDatabase::aqueousSpecies() const -> const std::vector<AqueousSpecies>&
{
	return pimpl->aqueous_species;
}

auto PhreeqcDatabase::gaseousSpecies(Index index) const -> GaseousSpecies
{
	return pimpl->gaseous_species[index];
}

auto PhreeqcDatabase::gaseousSpecies() const -> const std::vector<GaseousSpecies>&
{
	return pimpl->gaseous_species;
}
auto PhreeqcDatabase::mineralSpecies(Index index) const -> MineralSpecies
{
	return pimpl->mineral_species[index];
}

auto PhreeqcDatabase::mineralSpecies() const -> const std::vector<MineralSpecies>&
{
	return pimpl->mineral_species;	
}

auto PhreeqcDatabase::masterSpecies() const -> std::set<std::string>
{
	return pimpl->master_species;
}

auto PhreeqcDatabase::cross(const Database& reference_database) -> Database
{
    auto get_charge = [](std::string name) -> double
    {
        const auto idx_neg = name.find('-');
        if(idx_neg < name.size()) return std::min(-1.0, tofloat(name.substr(idx_neg)));

        const auto idx_pos = name.find('+');
        if(idx_pos < name.size()) return std::max(+1.0, tofloat(name.substr(idx_pos)));

        return 0.0;
    };

    // Return the Reaktoro name of a Phreeqc aqueous species
	auto reaktoro_naming = [&](std::string name) -> std::string
	{
		const double charge = get_charge(name);
		if(name == "H2O") return "H2O(l)";
		if(name == "CH4") return "Methane(aq)";
		if(charge == 0) return name + "(aq)";
		if(charge < 0) return name.substr(0, name.rfind('-')) + std::string(std::abs(charge), '-');
		else return name.substr(0, name.rfind('+')) + std::string(std::abs(charge), '+');
	};

	// The set of species that compose other species, not necessarily original master species.
	// Whenever a PHREEQC master species is not present in a reference database, an alternative
	// species in the reference database is sought to replace that PHREEQC master species.
	std::set<std::string> primary_species;

	// The set of PHREEQC master species that are not present in the reference database and
	// do not have an alternative species in the reference database to replace it.
	std::set<std::string> master_species_no_alternative;

	// Return the first product species in the reference database that can replace the given
	// master species and that is not in the set of primary species already.
	auto find_alternative_master_species = [&](Index imaster) -> std::string
	{
		const std::set<std::string>& products = pimpl->from_master_to_product_species[imaster];
		for(const std::string& product : products)
			if(reference_database.containsAqueousSpecies(reaktoro_naming(product)) ||
			   reference_database.containsGaseousSpecies(product) ||
			   reference_database.containsMineralSpecies(product))
			    if(!primary_species.count(product))
			        return product;
		return ""; // could not find an alternative species in the reference database
	};

	// Loop over all master aqueous species
	for(Index i = 0; i < pimpl->idx_master_species.size(); ++i)
	{
		const Index ispecies = pimpl->idx_master_species[i];
		const AqueousSpecies& species = pimpl->aqueous_species[ispecies];
		const std::string master_name = species.name();
		const std::string master_name_reaktoro = reaktoro_naming(master_name);

		// Check if the current master species is present in the given reference database.
		if(reference_database.containsAqueousSpecies(master_name_reaktoro))
			primary_species.insert(master_name);

		// Otherwise, find an alternative master species in the set of product species
		else
        {
		    const std::string alternative_species = find_alternative_master_species(i);

		    // Check if the alternative species was found (check for non-empty string)
		    if(alternative_species.size()) primary_species.insert(alternative_species);

		    // Store the name of the current PHREEQC master species with no alternative in
		    // the reference database.
		    else master_species_no_alternative.insert(master_name);
        }
	}

	Database database;

    // Return an AqueousSpecies instance with appropriate thermodynamic data.
    auto construct_aqueous_species = [&](const AqueousSpecies& species)
    {
        // Check if the aqueous species is a primary species
        if(primary_species.count(species.name()))
        {
            // Convert PHREEQC species name to Reaktoro species name
            const std::string reaktoro_name = reaktoro_naming(species.name());

            // Find the aqueous species in the reference database
            AqueousSpecies reference_aqueous_species =
                reference_database.aqueousSpecies(reaktoro_name);

            // Change the Reaktoro species name to PHREEQC name
            reference_aqueous_species.setName(species.name());

            return reference_aqueous_species;
        }

        // Check if the aqueous species is a master species with no
        // alternative replacement in the reference database
        if(master_species_no_alternative.count(species.name()))
        {
            // Create a AqueousSpeciesThermoData with zero coefficients in
            // the HKF thermodynamic parameters.
            AqueousSpeciesThermoData data;
            data.hkf.set(AqueousSpeciesThermoParamsHKF());

            // Create a copy of the given aqueous species and set its
            // thermodynamic data.
            AqueousSpecies copy = species;
            copy.setThermoData(data);

            return copy;
        }

        return species;
    };

    // Return a GaseousSpecies instance with appropriate thermodynamic data.
    auto construct_gaseous_species = [&](const GaseousSpecies& species)
    {
        // Check if the gaseous species is a primary species
        if(primary_species.count(species.name()))
            return reference_database.gaseousSpecies(species.name());
        return species;
    };

    // Return a MineralSpecies instance with appropriate thermodynamic data.
    auto construct_mineral_species = [&](const MineralSpecies& species)
    {
        // Check if the mineral species is a primary species
        if(primary_species.count(species.name()))
            return reference_database.mineralSpecies(species.name());
        return species;
    };

	for(auto& x : pimpl->aqueous_species)
	    database.addAqueousSpecies(construct_aqueous_species(x));

	for(auto& x : pimpl->gaseous_species)
	    database.addGaseousSpecies(construct_gaseous_species(x));

	for(auto& x : pimpl->mineral_species)
	    database.addMineralSpecies(construct_mineral_species(x));

	return database;
}

} // namespace Reaktoro
