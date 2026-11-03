#pragma once

#define CHLUPATE_ADDON_INFO_STR_ID 32000
#define CHLUPATE_MENU_STR_ID 32500
#define CHLUPATE_MENU_PROMPT_STR_ID 32501
#define CHLUPATE_PALETTE_ID 32420
#define CHLUPATE_SETTINGS_ID 32422
#define CHLUPATE_TOLERANCE_HELP_ID 32423

enum ChlupateMenuItemIds : short {
	ChlupateOpenPalette = 1
};

/* Paleta — musí odpovídat RINT 'GDLG' 32420. */
enum ChlupatePaletteControlIds : short {
	TxtTools			= 1,
	BtnWall				= 2,	/* IconPushCheck */
	BtnBeam				= 3,
	BtnColumn			= 4,
	BtnLine				= 5,
	BtnPolyLine			= 6,
	ChkSelectedOnly		= 7,
	BtnSettings			= 8,
	RadActiveView		= 9,
	RadWholeModel		= 10,
	RadHairy			= 11,
	RadModular			= 12,
	TxtModule			= 13,
	EditModuleMm		= 14,
	TxtTolLength		= 15,
	EditToleranceMm		= 16,
	TxtTolAngle			= 17,
	EditAngleTolerance	= 18,
	ResultList			= 19,
	TxtStatus			= 20,
	BtnReset			= 21,
	BtnCheck			= 22,
	BtnInfoTolLength	= 23,	/* Button Nápověda */
	BtnInfoTolAngle		= 24,	/* Button Nápověda */
	BtnTreeCheckAll		= 25,	/* Button: Vybrat vše */
	BtnTreeUncheckAll	= 26,	/* Button: Zrušit výběr */
	TreeChecks			= 27,	/* SingleSelTreeView: strom kontrol vlevo */
	SepVertical			= 28,	/* Separator: vertikální dělicí čára */
	TxtRightHeader		= 29,	/* LeftText: nadpis vybrané kontroly vpravo */
	TxtSubChecksHdr		= 30,	/* LeftText: popis / info k vybrané kontrole vpravo */
	BtnCheckSelected	= 31,	/* IconButton: Zkontrolovat pouze označené */
	BtnRecolorByTab		= 32,	/* Button: Přebarvit dle záložky */
	ChkLiveSync			= 33	/* CheckBox: Živá synchronizace */
};

/* Nápověda tolerancí 32423. */
enum ChlupateToleranceHelpIds : short {
	ToleranceHelpBtnOk		= 1,
	ToleranceHelpTxtBody	= 2
};

/* Nastavení 32422 — musí odpovídat RINT 'GDLG' 32422. */
enum ChlupateSettingsControlIds : short {
	SettingsBtnOk			= 1,
	SettingsBtnCancel		= 2,
	SettingsBtnDefault		= 3,
	SettingsChkEnable		= 4,
	SettingsSep1			= 5,
	SettingsTxtHitHdr		= 6,
	SettingsTxtHitColorLbl	= 7,
	SettingsUsrHitColor		= 8,
	SettingsBtnHitColor		= 9,
	SettingsTxtHitOpLbl		= 10,
	SettingsRealHitOp		= 11,
	SettingsTxtHitOpPct		= 12,
	SettingsTxtRestHdr		= 13,
	SettingsTxtRestColorLbl	= 14,
	SettingsUsrRestColor	= 15,
	SettingsBtnRestColor	= 16,
	SettingsTxtRestOpLbl	= 17,
	SettingsRealRestOp		= 18,
	SettingsTxtRestOpPct	= 19,
	SettingsSep2			= 20,
	SettingsTxtDecLbl		= 21,
	SettingsIntDecimals		= 22,
	SettingsSep3			= 23,
	SettingsTxtClassHdr		= 24,
	SettingsClassList		= 25,
	SettingsTxtDimPenLbl	= 26,
	SettingsIntDimPen		= 27,
	SettingsUsrDimPen		= 28,
	SettingsTxtDimCircleLbl	= 29,
	SettingsRealDimCircleMm	= 30
};

/* DG::Command ids (v rámci doplňku unikátní). */
enum CegraContextCommandId {
	CmdSelect2D		= 1,
	CmdSelect3D		= 2,
	CmdSelectZoom2D	= 3,
	CmdSelectZoom3D	= 4
};
