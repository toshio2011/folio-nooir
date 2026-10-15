#pragma once

class WebServer;

// Small web adapter for the existing per-book ClipFile JSON. It registers
// three routes and keeps each request bounded to one page or one book.
class WebClippingsHandler {
 public:
  static void registerRoutes(WebServer* server);
};
