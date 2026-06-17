param(
    [string]$Root = (Resolve-Path "$PSScriptRoot\..").Path
)

$ErrorActionPreference = "Stop"

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Message
    )

    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

function Assert-NotContains {
    param(
        [string]$Text,
        [string]$Pattern,
        [string]$Message
    )

    if ($Text -match $Pattern) {
        throw $Message
    }
}

$paramStoreC = Get-Content -Raw -Encoding UTF8 (Join-Path $Root "Hardware\param_store.c")
$paramStoreH = Get-Content -Raw -Encoding UTF8 (Join-Path $Root "Hardware\param_store.h")
$boardConfig = Get-Content -Raw -Encoding UTF8 (Join-Path $Root "Hardware\board_config.h")
$appState = Get-Content -Raw -Encoding UTF8 (Join-Path $Root "Hardware\app_state.c")
$menuC = Get-Content -Raw -Encoding UTF8 (Join-Path $Root "Hardware\menu.c")
$menuH = Get-Content -Raw -Encoding UTF8 (Join-Path $Root "Hardware\menu.h")

Assert-Contains $boardConfig "BOARD_PARAM_RUNTIME_SAVE_MS\s+3600000UL" `
    "runtime save throttle must be 1h (3600000UL)"
Assert-Contains $paramStoreC "PARAM_STORE_RECORDS_PER_PAGE" `
    "param_store must define records-per-page slot geometry"
Assert-Contains $paramStoreC "ParamStore_ScanPage" `
    "param_store must scan every slot in each Flash page"
Assert-Contains $paramStoreC "ParamStore_ReadSlot" `
    "param_store must read individual page slots"
Assert-Contains $paramStoreC "ParamStore_GetTargetLocation" `
    "param_store must choose append slot or page rollover target"
Assert-Contains $paramStoreC "ParamStore_ProgramSlot" `
    "param_store must program a slot without erasing the whole page every time"
Assert-Contains $paramStoreC "first_empty_slot" `
    "page scan must track the first erased slot for append"
Assert-Contains $paramStoreC "has_damaged_slot" `
    "page scan must detect damaged slots and force rollover"
Assert-Contains $appState "AppState_SaveEditedParameters" `
    "app_state must merge edited parameters with current runtime state"
Assert-Contains $menuH "params_save_request" `
    "menu intent must request parameter save instead of reporting completed save"
Assert-Contains $menuH "ParamStore_Record_t\s+params_record" `
    "menu intent must carry the edited parameter snapshot"
Assert-Contains $menuH "Menu_OnParamSaveResult" `
    "menu must expose a save-result callback for app_state"
Assert-NotContains $menuC "ParamStore_SaveParameters" `
    "menu.c must not write Flash directly when saving parameters"
Assert-NotContains ($menuC + $menuH + $appState) "params_saved" `
    "old params_saved intent name must be removed"

Assert-NotContains ($paramStoreC + $paramStoreH + $boardConfig + $appState) "10min|10 min|10 分钟" `
    "code comments must no longer describe runtime save throttle as 10min"

Write-Host "param_store log checks passed"
