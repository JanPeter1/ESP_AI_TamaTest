# Recherche: Tamagotchi, Figuren und emotionale Nachrichten

## Zielbild für DeskConnect CYD

Der spätere Startbildschirm soll eine kleine, lebendige Szene im Tamagotchi-Stil zeigen. Dort erscheinen Nachrichten anderer Teilnehmender im selben Raum. Zum Verfassen einer Nachricht wechselt man per Wischgeste auf einen eigenen Sende-Bildschirm mit thematisch sortierten Button-Sets. Optional sollen Nachrichten passende Avatar-Grafiken oder Figurenreaktionen enthalten.

## Erkenntnisse aus Tamagotchi

Das ursprüngliche Tamagotchi verbindet eine einfache, dauerhaft sichtbare Figur mit wiederholter Interaktion: Füttern, Spielen und Pflegen beeinflussen ihre Entwicklung. *Tamagotchi Connection* ergänzte soziale Aktionen zwischen Geräten: Figuren konnten einander besuchen, Spiele spielen und Geschenke austauschen. Für DeskConnect ist daran vor allem die Idee interessant, dass Nachrichten nicht bloß Text sind, sondern sichtbare soziale Ereignisse in einer gemeinsamen kleinen Welt.

Quellen: [Bandai: Original Tamagotchi](https://www.bandai.com/original-tamagotchi-white-with-black), [Tamagotchi Connection: Verbindungsfunktionen](https://tamagotchi-official.com/us/series/connection/news/01_430/), [Tamagotchi Connection: Spielanleitung](https://tamagotchi-official.com/us/series/connection/howto/).

## Emotionen mit Figuren darstellen

Eine Studie zu virtuellen Spielfiguren fand, dass eine stimmige Kombination aus Erscheinungsbild und Ausdruck Empathie und Immersion unterstützen kann. Eine größere kulturübergreifende Studie zeigt zugleich, dass Gesichtsausdrücke nicht immer eindeutig verstanden werden: Bedeutungen überlappen und unterscheiden sich teilweise zwischen Kulturen.

Daraus folgen praktische Regeln:

- Ausdruck über Gesicht, Körperhaltung und Kontext vermitteln, nicht nur über eine kleine Mundform.
- Für Nachrichten Figurreaktion und kurze Textbeschriftung kombinieren, zum Beispiel eine winkende Figur plus „Kaffee?“.
- Mit einer kleinen, klar unterscheidbaren Palette beginnen: neutral, fröhlich, müde, nachdenklich und überrascht.
- Ausdrücke an den konkreten Nachrichtentext koppeln; nicht voraussetzen, dass ein Gesicht allein die Absicht eindeutig erklärt.

Quellen: [Studie zu Aussehen und Ausdruck virtueller Figuren](https://journals.sagepub.com/doi/10.1177/1046878120926694), [Studie zu Gesichtsausdrücken in sechs Ländern](https://www.cell.com/iscience/fulltext/S2589-0042(24)00396-1).

## Vorgeschlagener Bedienfluss

1. **Startseite:** kleine Raum-Szene mit Figur(en), Raumname, letzter Nachricht und Hinweis auf weitere ungelesene Nachrichten.
2. **Eingang:** Nachricht als kompakte Sprechblase oder Ereigniskarte mit Absendername und Avatar; neue Nachrichten können eine kurze Figurenreaktion auslösen.
3. **Senden:** Wischgeste zur Sendeseite. Sichtbarer Seitenindikator und zusätzliche Schaltfläche sollen den Wechsel auch ohne erkannte Geste auffindbar machen.
4. **Button-Sets:** Kategorien wie „Stimmung“, „Alltag“, „Zuneigung“ und „Pause“. Sets auswählbar halten und pro Bildschirm wenige große, gut lesbare Buttons zeigen.

Der verfügbare Bildschirm hat 320 × 240 Pixel im Querformat. Nachrichten und Avatare sollten daher kompakt bleiben; wichtige Texte dürfen nicht nur in kleinen Sprechblasen stehen.

## Avatar-Bilder und Nachrichtenformat

Für den ersten Schritt einen kleinen Satz vorbereiteter Pixel-Avatare in der Firmware speichern. Nachrichten übertragen dann Kennungen und Inhalte statt bei jedem Tastendruck Bilddaten. LVGL 9 kann Bilder direkt aus eingebetteten C-Daten oder aus einem Dateisystem anzeigen ([LVGL Image Widget](https://lvgl.io/docs/open/9.1/widgets/image)). Später können ausgewählte Profilbilder einmalig übertragen und lokal zwischengespeichert werden.

Vorgeschlagene Felder: `room`, `sender_id`, `preset_id`, `text`, `avatar_id`, `emotion`, `timestamp`. Nachricht und Darstellung getrennt halten, damit ein Gerät die Nachricht auch ohne bereits verfügbares Avatar-Bild anzeigen kann.

## Umsetzungshinweise für dieses Projekt

- Der erste Umsetzungsschritt ist inzwischen integriert: Startbildschirm mit letztem Raumereignis, Katzen-/Hunde-Avatar, Wisch- und Button-Navigation zur Sendeseite, vier an-/abwählbare Themen-Sets und WebSocket-Nachrichten zum Raumserver.
- Das Nachrichtenprotokoll und die Serverrouten sind in `server/INTERFACE.md` dokumentiert. Es überträgt derzeit eine Avatar-Kennung, keine Bildbytes.
- LVGL 9 dokumentiert einfache Links-/Rechtsgesten über `LV_EVENT_GESTURE`. Bei Touch-Bedienung sollte die Geste trotzdem durch sichtbare Navigation ergänzt werden ([LVGL Gestures](https://lvgl.io/docs/open/9.5/main-modules/indev/gestures)).
- Weitere Ausbaustufen: sichtbare Reaktionen pro Emotionskennung, mehrere gleichzeitige Nachrichtenkarten, auswählbare persönliche Avatare und optionales Übertragen/Cachen kleiner Bilddateien.
