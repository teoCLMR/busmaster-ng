/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file      UpdateCheck.h
 * \brief     BUSMASTER-NG: Help > Check for Updates and Check at Startup
 *
 * Asks GitHub for the latest published release of BUSMASTER-NG and compares its version with the running one.
 * When a newer version exists and this copy was installed by the BUSMASTER-NG installer, the new installer is
 * downloaded, checked against the SHA-256 that GitHub gives for it and run without its wizard after BUSMASTER-NG
 * has closed; the installer starts BUSMASTER-NG again when it is done. Portable copies get the release page instead.
 */
#pragma once

/**
 * Help > Check for Updates: checks now and reports the result, whatever it is (message boxes owned by pParent, the
 * main frame). When the user accepts an update, pParent is closed (with the usual questions about saving) and is no
 * longer valid when this returns.
 */
void vCheckForUpdates(CWnd* pParent);

/** Help > Check at Startup, kept in HKCU\Software\BUSMASTER-NG\Updates (off unless the user turns it on). */
bool bIsStartupUpdateCheckOn();
void vSetStartupUpdateCheck(bool bOn);

/**
 * At startup, when Check at Startup is on: checks in a worker thread, so the start is not slowed down. Only a newer
 * version is reported, by posting WM_BMNG_UPDATE_FOUND to hMainFrame; errors (no network, ...) stay silent.
 */
void vStartStartupUpdateCheck(HWND hMainFrame);

/** Registered message posted by vStartStartupUpdateCheck; the main frame passes it to vOnUpdateFound. */
extern const UINT WM_BMNG_UPDATE_FOUND;

/** Offers the update found at startup (LPARAM of WM_BMNG_UPDATE_FOUND). Same as vCheckForUpdates for pParent. */
void vOnUpdateFound(CWnd* pParent, LPARAM lParam);
