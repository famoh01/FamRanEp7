// mobile/net_client/api_client.h
// Typed calls to the RAN MOBILE API (ran_mobile/Server). Keeps the Bearer token after login.
#pragma once

#include <functional>
#include <string>

#include "http_client.h"

namespace mobile::net {

// One request/response exchange. `path` starts with "/api/...". The transport decides the base URL
// (the POSIX client has host and port; the Android transport uses the build's API base URL).
// Returns false only on transport errors; HTTP error statuses come back in out.status.
using RequestFn = std::function<bool(const std::string& method, const std::string& path,
                                     const std::string& bearerToken, const std::string& jsonBody,
                                     HttpResponse& out, std::string& err)>;

struct WorldPoint {
    double x = 0.0;
    double y = 0.0;
};

class ServerApi {
public:
    explicit ServerApi(RequestFn request) : request_(std::move(request)) {}
    explicit ServerApi(HttpClient http)
        : request_([http](const std::string& m, const std::string& p, const std::string& t,
                          const std::string& b, HttpResponse& o, std::string& e) {
              return http.Request(m, p, t, b, o, e);
          }) {}

    bool Health(std::string& err) const;
    bool Register(const std::string& username, const std::string& password, std::string& err) const;
    bool Login(const std::string& username, const std::string& password, std::string& err);
    bool ListCharacters(std::string& json, std::string& err) const;
    bool ListMonsters(std::string& json, std::string& err) const;
    // class: "fighter", "mage" or "archer" (the server's CLASS_START_STATS keys). Returns the new character id.
    bool CreateCharacter(const std::string& name, const std::string& gender, const std::string& cls,
                         long long& characterId, std::string& err) const;
    bool DeleteCharacter(long long characterId, std::string& err) const;
    bool GetCharacter(long long characterId, std::string& json, std::string& err) const;

    // World (ดู ran_mobile/Server/ranserver/world.py). Positions are fractions 0..1 of the map.
    // EnterWorld returns the full JSON (character + world: map, blocks, monsters, position).
    bool EnterWorld(long long characterId, std::string& json, std::string& err) const;
    bool LeaveWorld(long long characterId, std::string& err) const;
    bool GetWorldState(long long characterId, std::string& json, std::string& err) const;
    // POST .../world/move. Accepted: pos is the accepted point. Rejected: pos is the server's position
    // (the client must snap to it) and err holds the server code (position_rejected, blocked).
    // SessionLost: the server ended the world session (409 not_in_world); pos is the saved position,
    // and the client should re-enter with ResumeWorld. Unauthorized: the login token is no longer
    // valid (401); the user must log in again. Throttled: requests came too close together
    // (429 move_too_fast); the move was not judged, so the client keeps the position and tries
    // again on a later tick. Failed: transport or unexpected error.
    enum class MoveResult { Accepted, Rejected, SessionLost, Unauthorized, Throttled, Failed };
    MoveResult MoveCharacter(long long characterId, double x, double y, WorldPoint& pos, std::string& err) const;
    // Ends the server session (POST /api/auth/logout) and forgets the local token. The token is
    // forgotten even if the server call fails, so the app never keeps using a stale token.
    bool Logout(std::string& err);
    void ClearToken() { token_.clear(); }
    bool LoggedIn() const { return !token_.empty(); }

    const std::string& Token() const { return token_; }

private:
    // Sends one request; a 4xx/5xx status becomes an error string containing the server's error code.
    bool Call(const std::string& method, const std::string& path, const std::string& body,
              HttpResponse& out, std::string& err) const;

    RequestFn request_;
    std::string token_;
};

}  // namespace mobile::net
