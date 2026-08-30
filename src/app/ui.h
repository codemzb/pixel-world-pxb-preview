// ui.h
//
// Dear ImGui-based user interface. Pure presentation: it reads the App state
// and calls back into App for actions. No business logic lives here.
//
#pragma once

namespace pxb {

class App;
class Renderer;

void render_ui(App& app, Renderer& renderer);

} // namespace pxb
