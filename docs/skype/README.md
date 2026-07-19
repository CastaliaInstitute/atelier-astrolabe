# Astrolabe Skype pairing

`/skype/` authorizes Spotify with OAuth PKCE, then transfers the resulting
refresh token and public client ID to Astrolabe over Web Bluetooth. Astrolabe
stores the credential in NVS and calls the Spotify Web API directly.

## Spotify application setup

The Spotify application with client ID
`5c02a959bc894e6ba0d5c338f7bcc60a` must allow this exact redirect URI:

```text
https://astrolabe.castalia.institute/skype/
```

The PWA requests only these scopes:

- `user-read-playback-state`
- `user-modify-playback-state`

Spotify playback-control endpoints require a Premium account and an active
Spotify Connect device. Astrolabe controls that device; it is not itself an
audio playback endpoint.

## Pairing

1. Open `/skype/` in a Web Bluetooth-capable Chromium browser.
2. Authorize Spotify.
3. Keep Astrolabe awake and select it in the Bluetooth chooser.
4. Open the Spotify face and tap the record to play or pause.

The browser clears its session copy of the refresh token after the BLE write.
Re-authorize when the Spotify refresh token expires or is revoked.
