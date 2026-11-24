<p align="center"><img src="images/Logo%20minimalista%20Small%20TV%20Evolution%20Pro.PNG" alt="Small TV Evolution Pro" width="480"></p>

# Small TV Evolution Pro (V.2.0.4) - AI Powered

Firmware personalizzato in italiano per **NM-TV-154** (ESP32-WROOM-32E, 4 MB, display ST7789 240x240).
Dati meteo, qualita' dell'aria, UV e pollini da [Open-Meteo](https://open-meteo.com) (gratis, nessuna chiave API).

## Avvio

All'accensione compare il **logo Small TV Evolution Pro** con la versione e l'indirizzo IP sotto. In questi secondi **non toccare il pulsante**: il sensore touch si calibra. Poi appare il menu.

## Uso del pulsante touch (in alto)

| Dove | 1 tocco veloce | 2 tocchi veloci | Tieni premuto |
|---|---|---|---|
| **Menu** (Meteo / Aria / Fasi Lunari / Radar ISS / Radar Voli / Cripto / Bibigotchi / Bibi AI / Foto / Quadranti / Frattali / Pomodoro / Timer / Countdown / Citazione / Numeri / Post-it / Sistema) | schermata successiva | schermata precedente | entra |
| **Dentro una schermata** | funzione successiva (ciclico) | funzione precedente | torna al menu |

Tieni premuto **8 secondi** (da qualsiasi punto) per aprire il portale di configurazione.

Gli stessi comandi sono nell'interfaccia web, sopra: **freccia sinistra** = doppio click, **entra/esci** = pressione lunga, **freccia destra** = click singolo.

## Schermate

Nel menu restano solo il titolo, la grafica e i pallini di scorrimento.

- **Meteo**
  1. Principale: orologio grande con barra dei secondi, data in italiano con anno, icona meteo, temperatura, riquadri centrati Min / Max / Umidita'. La riga in basso cambia ogni 4 s: condizioni, temperatura percepita, vento, pressione (Atm), alba, tramonto.
  2. Previsioni dei prossimi 3 giorni.
  3. Grafico della temperatura delle prossime 24 ore, con ora attuale, minimo e massimo.

  Nella schermata principale, a destra della temperatura, una **freccia di tendenza** (disegnata a codice): su = in aumento, giu = in calo, **doppia freccia sinistra/destra** = stabile (stima sulle prossime 3 ore).
- **Aria**
  1. Indice europeo EAQI su quadrante a 6 fasce con indicatore, livello a parole (ottima, buona, discreta, scadente, molto scadente, pessima), PM2.5 e PM10 con barretta colorata.
  2. **Salute** (come in SmallTV Evolution): indice UV (numero grande, livello, consiglio, scala a 5 fasce con indicatore e massimo previsto oggi) e pollini di graminacee, olivo e betulla (da assenti a molto alti). I pollini sono forniti solo per l'Europa e in stagione: altrimenti la schermata lo indica.

- **Fasi Lunari**: il disco della luna disegnato a codice con la fase di oggi (mari, crateri, alone e stelle), il nome della fase, la percentuale illuminata (in crescita o in calo) e la data della prossima luna piena. Calcolo astronomico locale, nessuna connessione necessaria oltre all'ora. **Tieni premuto** per tornare al menu.
- **Radar ISS**: mappa del mondo a punti con giorno e notte, traccia dell'orbita (passato e futuro), posizione in tempo reale della Stazione Spaziale con onda animata, la tua citta' evidenziata, coordinate, distanza, **prossimo passaggio** sopra di te (ora, tra quanto, elevazione massima, durata) e indicazione di **visibile a occhio nudo**. La posizione arriva da Open Notify (nessuna chiave API) solo mentre la schermata e' aperta; il modello orbitale si ricava in circa un minuto. **Tieni premuto** per tornare al menu.
- **Radar Voli**: radar scenografico con gli **aerei in volo vicino alla tua citta'** (raggio 25-200 km dal web, predefinito 50). Raggio che gira, cerchi concentrici, ogni aereo e' un triangolino orientato come la sua rotta e colorato per quota (verde basso, giallo medio, azzurro alto), con posizione che si muove tra un aggiornamento e l'altro. Sotto: **codice volo**, **quota in metri**, **rotta** (partenza > destinazione con citta') e velocita'. **1 tocco** = aereo successivo, **2 tocchi** = precedente, **pressione lunga** = menu. Dati ADS-B aperti da adsb.fi (con adsb.lol di riserva) e rotte da adsbdb: nessuna chiave API, letti solo con la schermata aperta. La rotta non e' nota per tutti i voli (in quel caso compare "Rotta sconosciuta").
- **Cripto**: andamento in tempo reale delle criptovalute scelte dal web (le 20 principali: BTC, ETH, BNB, XRP, SOL, DOGE, ADA, TRX, AVAX, LINK, DOT, TON, SHIB, LTC, BCH, XLM, UNI, ATOM, NEAR, POL). Per ognuna: prezzo, variazione sul periodo mostrato e **grafico a candele** (verdi = rialzo, rosse = ribasso) con minimo, massimo e ultimo prezzo. Si passa da una cripto all'altra da sole ogni N secondi (1-60, predefinito 5); con **1 tocco** (avanti) o **2 tocchi** (indietro) si cambia subito, senza aspettare. Pallini e barra in basso mostrano quale cripto e' in corso e quanto manca. I dati arrivano da Binance (dati pubblici, nessuna chiave API) e vengono letti solo mentre la schermata e' aperta.
- **Bibigotchi**: Bibi, un piccolo **barboncino nero** pixel-art animato (il suo viso e' anche l'icona nel menu). Cambia umore in base ai dati che il firmware ha gia': temporale, notte (dorme), feste fisse (Capodanno, Epifania, 25 aprile, 1 maggio, 2 giugno, Ferragosto, Ognissanti, Immacolata, Natale, Santo Stefano, San Silvestro), crollo o rialzo forte delle cripto (almeno 5%, dati della schermata Cripto non piu' vecchi di 3 ore), aria pessima (mascherina), pollini alti (starnuti), pioggia, neve, caldo (30 C o piu'), freddo (3 C o meno), UV alto, caffe' del mattino, pappa a pranzo e cena; poi 4 umori allegri di tutti i giorni. Con **1 tocco** (avanti) o **2 tocchi** (indietro) scorri gli stati attuali di Bibi; **pressione lunga** per tornare al menu.
- **Bibi AI**: Bibi diventa un **assistente AI**. Gli scrivi dalla **chat nell'interfaccia web** (es. "Bibi, come sara' il tempo domani?"): sul display Bibi si mette in **modalita' pensierosa** con un'animazione mentre attende, poi riceve la risposta, **cambia espressione** in base a come risponde (felice, innamorato, festa, sorpreso, triste, pensieroso, sonno, freddo, caldo, pioggia, neve, tempesta, rialzo, crollo, mascherina) e mostra un **messaggio breve**; la risposta completa e' nella chat. Conosce meteo (oggi e prossimi giorni), qualita' dell'aria e prezzi delle cripto e li commenta; **ricorda la conversazione** (ultimi 6 scambi, anche dopo il riavvio) e genera **una frase diversa ogni giorno**. Col touch: **1 tocco** = commento di Bibi (a rotazione: meteo, aria, cripto, consiglio del giorno), **2 tocchi** = rilegge le risposte precedenti, **pressione lunga** = menu. L'intelligenza artificiale gira **su un servizio esterno** (non sull'ESP32): scegli dal web **Claude (Anthropic)** con la tua chiave API oppure un servizio **compatibile OpenAI** (indirizzo https, modello e chiave a scelta; l'indirizzo predefinito e' un servizio gratuito senza chiave). La chiave resta salvata nel dispositivo e non viene mai mostrata nella pagina.
- **Foto**: le foto caricate dall'interfaccia web, mostrate a giro per i secondi scelti (1-60, predefinito 5). Con **1 tocco** (avanti) o **2 tocchi** (indietro) cambi foto subito, senza aspettare.
- **Quadranti**: cinque orologi, si cambia con 1 tocco (avanti) / 2 tocchi (indietro). Il quinto, **Orologi nel mondo**, mostra l'ora di **4 citta' scelte dal web** (33 citta' disponibili; ora legale calcolata in automatico), con sole/luna, giorno e differenza rispetto a Roma
  1. **Semplice**: ora grande, data, icona meteo e temperatura.
  2. **Analogico**: quadrante rotondo con lancette di ore, minuti e secondi.
  3. **Flip**: cartellini che si girano per ore, minuti e secondi.
  4. **Matrix**: pioggia di caratteri verdi con ora, data e temperatura.
  5. **Orologi nel mondo**.
- **Frattali**: Mandelbrot, Julia e altri frattali che si disegnano a blocchi sempre piu' fini; i "Julia in movimento" restano animati. Il nome compare in basso a sinistra a disegno finito. **1 tocco** = frattale successivo, **2 tocchi** = precedente (il primo e' quello del giorno).
- **Pomodoro**: timer di concentrazione (25 min di lavoro, 5 di pausa, ogni 4 pomodori una pausa lunga di 15 min: tempi modificabili dal web). **Avanti** = avvia, di nuovo avanti = pausa, di nuovo = riprende. **Indietro** o **tieni premuto** = azzera e riparti da capo; con il timer gia' azzerato, tieni premuto per tornare al menu. Anello di avanzamento colorato per fase, pomodori completati e lampeggio al cambio fase.
  - **Livelli e XP** (seconda funzione: con il timer fermo fai **2 tocchi**; da li' un tocco torna al timer, pressione lunga al menu): ogni pomodoro di lavoro completato vale **25 XP**, raggiungere l'**obiettivo giornaliero** (1-12 pomodori, predefinito 4, dal web) altri **50 XP**. Mostra livello e titolo (Cucciolo, Curioso, Studioso, Concentrato, Maestro, Guru, Leggenda), barra verso il livello successivo, obiettivo di oggi, **serie di giorni** consecutivi, **statistiche degli ultimi 7 giorni** con linea dell'obiettivo e **8 badge** (primo pomodoro, 10, 50 e 100 pomodori, obiettivo raggiunto, serie di 3 e di 7 giorni, livello 5). Quando raggiungi un obiettivo, un badge o un nuovo livello **Bibi festeggia** con coriandoli.
- **Timer**: conto alla rovescia (predefinito 0 ore, 10 minuti, 0 secondi). **Avanti** = avvia, di nuovo avanti = pausa, di nuovo = riprende. **Indietro** (2 tocchi veloci) = torna al valore impostato; **tieni premuto** = esce al menu. Barra di avanzamento verde/arancio/rossa e lampeggio a fine tempo.
- **Countdown**: conto alla rovescia per fino a 3 eventi (vacanze, compleanni, Natale...) impostati dal web: anello colorato che si riempie avvicinandosi alla data, giorni mancanti, nome e data; il giorno dell'evento compare "OGGI!" con i coriandoli. **1 tocco** = evento successivo, **2 tocchi** = precedente.
- **Citazione**: una citazione del giorno (letteratura italiana, 59 frasi) con virgolette decorative, accenti e autore; cambia a mezzanotte. **1 tocco** = citazione successiva, **2 tocchi** = precedente.
- **Numeri**: i 5 numeri fortunati del giorno in palline colorate piu' il numero Jolly. **1 tocco** = i numeri del giorno dopo, **2 tocchi** = del giorno prima.
- **Post-it**: una nota su carta colorata (fino a 3, scritte dal web, max 40 caratteri) con le emoticon disegnate a codice (`:) ;) :D :( :P :O <3 8) :'( ^_^`). **1 tocco** = nota successiva, **2 tocchi** = precedente.
Tutte e cinque: **tieni premuto** per tornare al menu.
- **Sistema**
  1. **Matrix**: pioggia di caratteri verdi con un pannello di statistiche: nome della rete Wi-Fi, indirizzo IP, qualita' del segnale (a parole, percentuale e dBm), uptime e memoria libera.

Aggiornamento automatico: meteo ogni 10 minuti, aria/salute ogni 30 minuti. Ora via NTP con fuso Europe/Rome (ora legale/solare automatica).

## Temi

Gli stessi 5 temi di SmallTV Evolution: **Neon** (cyberpunk, predefinito), Aurora, Tramonto, Ghiaccio, Ambra. Si scelgono dall'interfaccia web.

## Interfaccia web

Apri `http://nmtv.local` oppure l'indirizzo IP mostrato all'avvio:

- tre tasti di navigazione (indietro, entra/esci, avanti) con la schermata attuale;
- rete WiFi, citta', formato ora 24 h / 12 h (AM/PM), tema, luminosita', inversione colori, rotazione;
- **Aggiorna**: applica subito sul dispositivo le opzioni cambiate (tema, luminosita', ecc.) senza riavviare (anche cambiando rete WiFi o citta');
- **Cripto**: scegli quali criptovalute mostrare, per quanti secondi (1-60) e l'intervallo delle candele (5 min, 15 min, 1 ora, 4 ore);
- **Foto**: secondi per ogni foto (1-60) e come vengono caricate: **adattate automaticamente** (riempiono lo schermo, ritagliando) oppure **intere con bordi neri**. sezione **Foto caricate**: scegli le immagini, vengono portate a 240x240 e compresse nel browser (8-15 KB ciascuna), massimo 20; anteprime con elimina;
- **Salvaschermo**: attivazione, minuti di inattivita' (1-60, predefinito 5), secondi per ogni funzione (1-60, predefinito 5) e quali funzioni mostrare tra tutte quelle presenti;
- **Pomodoro**: minuti di lavoro, pausa e pausa lunga, obiettivo giornaliero e azzeramento di XP e statistiche;
- **Bibi AI**: servizio (Claude o compatibile OpenAI), indirizzo, modello, chiave API, frase del giorno; sotto il modulo la **chat con Bibi** con memoria e pulsanti per cancellarla e chiedere la frase del giorno;
- **Radar voli**: raggio 25, 50, 100, 150 o 200 km;
- **Orologi nel mondo**: le 4 citta' del quinto quadrante;
- **Timer**: ore, minuti e secondi di partenza del conto alla rovescia;
- **Countdown**: nome, data e "si ripete ogni anno" per 3 eventi; **Post-it**: testo e attivazione di 3 note;
- **Schermata all'avvio**: scegli un orologio (o un'altra funzione) e all'accensione il dispositivo entra direttamente li'; con **Aggiorna** ci va subito;
- **Riavvia**: riavvia il dispositivo (le modifiche non applicate con Aggiorna vanno perse);
- **modalita' notturna**: fascia oraria (Dalle / Alle) e luminosita' notturna 0-100% (0 = schermo spento);
- **aggiornamento firmware da browser (OTA)**, senza cavo;
- in fondo, **ripristino di fabbrica** (cancella tutte le impostazioni e riavvia in configurazione).

## Aggiornamento senza cavo (OTA)

1. Apri l'interfaccia web, sezione **Aggiornamento firmware (OTA)**.
2. Scegli `nmtv-firmware-app.bin` (**non** quello "completo": il dispositivo lo rifiuta) e premi **Aggiorna**.
3. Attendi la barra al 100%: il dispositivo si riavvia da solo. Le impostazioni restano.

**Dalla 1.0.4** si aggiorna dal browser con `nmtv-firmware-app.bin`: la 2.0.4 si installa cosi', senza cavo, e le impostazioni restano. Il flash via cavo con il file "completo" serve solo se parti da una versione precedente alla 1.0.4.

## Novita' 2.0.4

- Nuova schermata **Bibi AI** (dopo Bibigotchi): assistente con chat dal web, espressioni animate, commenti su meteo, aria e cripto, memoria della conversazione e frase del giorno; l'AI gira su un servizio esterno.
- Nuova schermata **Radar Voli** (dopo Radar ISS): aerei in volo vicino a te su un radar animato, con codice volo, destinazione e quota.
- **Quadranti**: quinto orologio con 4 citta' del mondo scelte dal web.
- **Pomodoro**: seconda funzione **Livelli e XP** con obiettivi giornalieri, badge, statistiche settimanali e Bibi che festeggia.
- Il menu passa a 18 schermate (26 funzioni): la schermata all'avvio e le funzioni del salvaschermo gia' salvate si spostano da sole.
- Aggiornamento via `nmtv-firmware-app.bin` dalla 2.0.3, nessun cambio di partizioni.

## Novita' 2.0.3

- Nuova schermata **Timer** dopo Pomodoro (il menu passa a 16 schermate; scelta all'avvio e salvaschermo salvati si spostano da soli).
- Interfaccia web: ore, minuti e secondi del Timer.
- **Flip Watch**: animazione dei cartellini piu' fluida, con giro a pagina in molti fotogrammi.
- Aggiornamento via `nmtv-firmware-app.bin` dalla 2.0.2, nessun cambio di partizioni.

## Novita' 2.0.2

- **Salvaschermo** (impostabile dal web): se attivo, dopo il tempo di inattivita' scelto (1-60 minuti, predefinito 5) il dispositivo mostra le funzioni che hai selezionato: una sola resta fissa, piu' di una vanno a giro (1-60 secondi ciascuna, predefinito 5). Un tocco al pulsante lo ferma e torna alla schermata di prima.
- Icone del menu di **Frattali** e **Numeri**: i rettangoli non coprono piu' il cerchio ne' le palline tra loro.
- Nessun cambio di partizioni: dalla 2.0.1 basta `nmtv-firmware-app.bin` da browser.

## Novita' 2.0.1

- **Meteo**: i due punti dell'orario lampeggiano ogni secondo (anche nel quadrante semplice).
- Nuove schermate **Frattali** (dopo Quadranti), **Countdown**, **Citazione**, **Numeri** e **Post-it** (dopo Pomodoro, prima di Sistema). Il menu passa a 15 schermate e la scelta "schermata all'avvio" salvata si sposta da sola.
- Web: nuove sezioni per gli eventi del Countdown e le note del Post-it.
- Nessun cambio di partizioni: dalla 2.0.0 basta `nmtv-firmware-app.bin` da browser.

## Novita' 2.0.0

- **Loghi**: logo futuristico all'avvio e logo minimalista nell'interfaccia web (JPEG da pochi KB nella flash del firmware, non nella memoria delle foto); il logo minimalista e' anche in cima a questo README (cartella `images`).
- Nuove schermate **Fasi Lunari** e **Radar ISS** (dopo Aria, prima di Cripto). Il menu passa a 10 schermate; la scelta "schermata all'avvio" salvata si sposta da sola.
- **Meteo**: con temperatura stabile la freccia di tendenza diventa una doppia freccia sinistra/destra.
- Nessun cambio di partizioni: dalla 1.0.9a basta `nmtv-firmware-app.bin` da browser.

## Novita' 1.0.9

- Nuova schermata **Bibigotchi** (dopo Cripto): il barboncino nero Bibi cambia umore in base a meteo, aria, UV, pollini, ora, feste e cripto.
- **Foto con colori fedeli**: le foto sono ora decodificate con lo stesso decoder JPEG dell'altro progetto (niente piu' aspetto sbiadito); tolta la scelta di luminosita' dal web.
- **Pomodoro**: i testi al centro non coprono piu' l'anello.
- Nessun cambio di partizioni: dalla 1.0.8 basta `nmtv-firmware-app.bin` da browser.

## Novita' 1.0.8

- Nuova schermata **Foto** (dopo Cripto) con caricamento dal web, conversione e compressione nel browser, max 20 foto.
- Nuova schermata **Pomodoro** (dopo Quadranti).
- **Nuova tabella delle partizioni** (`partitions_nmtv.csv`): 704 KB per le foto. Vedi la nota qui sotto.

**Importante per la 1.0.8:** per avere tutto lo spazio per 20 foto serve flashare **una volta via cavo** il file `nmtv-firmware-completo.bin` all'indirizzo `0x0` (la tabella delle partizioni non si cambia con l'OTA). Le impostazioni salvate (WiFi, citta', ecc.) restano. Se invece aggiorni solo da browser con `nmtv-firmware-app.bin`, tutto funziona ma lo spazio per le foto e' ridotto (circa 6-8 foto); la pagina web mostra lo spazio libero e te lo segnala.

## Novita' 1.0.7

- Nuova schermata **Cripto** (tra Aria e Quadranti) con candele in tempo reale, rotazione automatica e scelta dal web.

## Novita' 1.0.6

- Aggiornamento meteo ogni 10 minuti (prima 15).
- Il pulsante "Salva e riavvia" diventa **Riavvia**.
- Freccia di tendenza della temperatura nel Meteo.
- Nuova schermata **Quadranti** con 4 orologi, selezionabili anche dal web (avvio diretto).

## Novita' 1.0.5

- Nuova schermata **Sistema** con interfaccia Matrix e statistiche.
- Pulsante **Aggiorna** nell'interfaccia web.
- Modalita' notturna per la luminosita' dello schermo.
- Ripristino di fabbrica dall'interfaccia web.

## Primo flash

**Prima fai un backup del firmware originale** (SCELTA CONSIGLIATA!):

```
python -m esptool --chip esp32 --port COMx read_flash 0 0x400000 backup-originale.bin
```

Poi scrivi il firmware (cambia `COMx` con la tua porta):

```
python -m esptool --chip esp32 --port COMx --baud 460800 write_flash 0x0 nmtv-firmware-completo.bin
```

In alternativa un flasher web da Chrome/Edge (es. https://web.esphome.io) con il file `.bin` completo.

## Prima configurazione

Al primo avvio compare "Configurazione" sullo schermo:

1. Collegati dal telefono alla rete WiFi **NM-TV-Setup**.
2. Apri `192.168.4.1` nel browser (di solito si apre da solo).
3. Scegli la rete WiFi, inserisci la password, la **citta'**, il formato ora e il tema. Salva.

## Se lo schermo non e' perfetto

- Sembra un negativo: l'opzione "Inverti i colori" e' attiva di default; se serve, toglila dall'interfaccia web.
- Sottosopra: spunta "Ruota lo schermo di 180 gradi".
- Rosso e blu scambiati: in `platformio.ini` togli il `;` dalla riga `-DTFT_RGB_ORDER=TFT_BGR` e ricompila.
- Il pulsante risponde male: in `platformio.ini` togli il `;` da `-DTOUCH_DEBUG=1` per vedere i valori del sensore in basso nel menu.

## Pin usati

| Funzione | GPIO |
|---|---|
| SPI CLK / MOSI | 14 / 13 |
| DC / CS | 2 / 15 |
| RST | non collegato |
| Backlight (PWM, attivo basso) | 19 |
| Alimentazione pannello (LOW) | 21 |
| Touch | 32 (T9) |

I testi sullo schermo non usano lettere accentate (i font integrati non le hanno), la pagina web invece si'.
