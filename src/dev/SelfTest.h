// SPDX-License-Identifier: GPL-3.0-only
/*
 *  The check that keeps the rendering honest.
 *
 *  Every surface this launcher draws is supposed to be one of two things: the paper, or the plum.
 *  A widget that comes out black, transparent, or in some colour the theme never mentions is the
 *  class of bug that cost days on the old code, so it is checked by rendering the widget and
 *  looking at the pixels, not by trusting the stylesheet.
 */

#pragma once

namespace Komorebi::Dev {

/// Renders the controls and surfaces the launcher uses and reports whether each one came out as
/// the theme says it should. Returns the number of failures.
int runSelfTest();

}  // namespace Komorebi::Dev
