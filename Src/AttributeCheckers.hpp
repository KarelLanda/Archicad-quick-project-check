#pragma once

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Array.hpp"
#include "HashSet.hpp"
#include "UniString.hpp"

enum class AttributeSubCheckId : short {
	MissingInAttributes		= 0,
	MissingTextures			= 1,
	UnclassifiedAttributes	= 2,
	DuplicatedAttributes	= 3,
	Count					= 4
};

struct AuditColumnDef {
	GS::UniString	title;
	short			width = 180;
};

struct AuditRow {
	GS::Array<GS::UniString>	cells;
	API_AttrTypeID				attrType		= API_ZombieAttrID;
	API_AttributeIndex			attrIndex		= APIInvalidAttributeIndex;
	API_Guid					elemGuid		= APINULLGuid;
	bool						is3DCapable		= false;
	Int32						subElemIndex	= -1;
	bool						hasMarkerPos	= false;
	API_Coord					markerPos		= { 0.0, 0.0 };
	API_ElemType				toolType		= API_ZombieElemID;
	GS::UniString				favoriteName;
};

struct SubCheckState {
	GS::UniString				baseName;
	bool						checked		= true;
	bool						hasRun		= false;
	GS::Array<AuditColumnDef>	columns;
	GS::Array<AuditRow>			rows;
};

void InitAttributeSubChecks (GS::Array<SubCheckState>& outSubChecks);

void RunAttributeSubCheck (
	AttributeSubCheckId				subCheckId,
	const GS::HashSet<API_Guid>&	disabledClassificationSystems,
	SubCheckState&					inOutState);
