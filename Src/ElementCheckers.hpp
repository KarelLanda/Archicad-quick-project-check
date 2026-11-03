#pragma once

#include "AttributeCheckers.hpp"

enum class ElementSubCheckId : short {
	MissingAttributesInElements	= 0,
	UnclassifiedElements		= 1,
	UndefinedStructuralFunction	= 2,
	UndefinedPosition			= 3,
	ElementsOnArchicadLayer		= 4,
	Count						= 5
};

enum class PropertySubCheckId : short {
	UnassignedProperties		= 0,
	Count						= 1
};

enum class ViewSubCheckId : short {
	UnusedPenTables				= 0,
	UnusedLayerCombinations		= 1,
	UnusedRenovationFilters		= 2,
	Unused3DStyles				= 3,
	UnusedGraphicOverrides		= 4,
	UnusedModelViewOptions		= 5,
	Count						= 6
};

enum class DocSubCheckId : short {
	UnassociatedLabels			= 0,
	CustomTextDimensions		= 1,
	StaticDimensions			= 2,
	Count						= 3
};

void InitElementSubChecks (GS::Array<SubCheckState>& outSubChecks);

void RunElementSubCheck (
	ElementSubCheckId				subCheckId,
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState);

void InitDefaultSubChecks (GS::Array<SubCheckState>& outSubChecks);

void RunDefaultSubCheck (
	ElementSubCheckId				subCheckId,
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState);

void InitFavoriteSubChecks (GS::Array<SubCheckState>& outSubChecks);

void RunFavoriteSubCheck (
	ElementSubCheckId				subCheckId,
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState);

void InitPropertySubChecks (GS::Array<SubCheckState>& outSubChecks);

void RunPropertySubCheck (
	PropertySubCheckId				subCheckId,
	SubCheckState&					inOutState);

void InitViewSubChecks (GS::Array<SubCheckState>& outSubChecks);

void RunViewSubCheck (
	ViewSubCheckId					subCheckId,
	SubCheckState&					inOutState);

void InitDocSubChecks (GS::Array<SubCheckState>& outSubChecks);

void RunDocSubCheck (
	DocSubCheckId					subCheckId,
	SubCheckState&					inOutState);
