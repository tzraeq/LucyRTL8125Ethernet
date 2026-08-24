# MAC-cím és az OpenCore injektálásának következtetései

## 1. A jelenlegi driver MAC-címének beolvasási és érvényesítési folyamata

- A driver indulásakor a `start()` meghívja az `initRTL8125()` függvényt, amely elvégzi a MAC-cím inicializálását.
- Az `initRTL8125()` először a hardver `MAC0..MAC5` regisztereiből olvassa ki a MAC-címet; bizonyos chipkonfigurációk esetén elsőbbséget élvezhet a `BACKUP_ADDR0_8125/BACKUP_ADDR1_8125`.
- Ha a beolvasott cím érvényes, közvetlenül beírja azt a hardver vételi címregiszterébe (`rtl8125_rar_set()` -> `MAC0/MAC4`).
- Ha a beolvasott eredmény érvénytelen, a jelenlegi kódág a `fallbackMAC` értékét használja.
- A külvilág felé jelentett MAC-címet a `getHardwareAddress()` adja vissza a `currMacAddr` értékéből; futás közben a `setHardwareAddress()` módosíthatja a `currMacAddr` értékét, majd meghívja a `rtl8125_rar_set()` függvényt.

## 2. A `fallbackMAC` tényleges működése

- A `fallbackMAC` forrása az `Info.plist` `Driver Parameters` szótára (a jelenlegi kódban).
- Az „érvényes MAC-cím” ellenőrzésére az `is_valid_ether_addr()` szolgál:
  - nem lehet teljesen nulla;
  - nem lehet multicast cím (az első bájt legalsó bitje nem lehet 1);
  - a `FF:FF:FF:FF:FF:FF` cím szintén érvénytelennek minősül a multicast-szabály miatt.
- Fontos: a `fallbackMAC` a jelenlegi kódban **csak akkor vesz részt**, ha a hardverből kiolvasott MAC-cím érvénytelen; alapértelmezés szerint tehát **nem írja felül a hardver érvényes eredeti MAC-címét**.
- Fontos: a jelenlegi kód **újra ellenőrzi** a `fallbackMAC`-ot (a hozzá tartozó `fallBackMacAddr` értéket); csak akkor használja, ha az is érvényes. Ellenkező esetben tovább próbálkozik a `BACKUP_ADDR` értékekkel, végül pedig véletlenszerű MAC-címet generál.

## 3. A jelenlegi kód beolvassa-e az OpenCore `DeviceProperties` értékeit?

Következtetés: **a jelenlegi kód nem olvassa az OC által az `IOPCIDevice` csomópontjába injektált tulajdonságokat.**

- A jelenlegi implementáció maga a driver szolgáltatásának tulajdonságait olvassa: `getProperty("Driver Parameters")`.
- Nem található olyan logika, például `pciDevice->getProperty(...)`, amely a providertől (`IOPCIDevice`) olvasna be injektált tulajdonságokat.
- Ezért a jelenlegi állapotban a `DeviceProperties -> Add -> PciRoot(...)/Pci(...)` alatt megadott egyéni kulcsokat a driver alapértelmezés szerint nem használja fel.

## 4. Az OC által injektált tulajdonságok helyes beolvasási módja

Az OpenCore dokumentációja és a közösségi gyakorlat alapján a `DeviceProperties` bekerül az IORegistry-be, ezért a drivernek a megfelelő eszközcsomópontból kell kiolvasnia:

- A driverben elsődlegesen az `IOPCIDevice`-től (providertől) kell olvasni, például:

```cpp
pciDevice->getProperty("mac-address")
```

- Ha ez nem található, akkor vissza lehet térni az `Info.plist` `Driver Parameters` értékéhez (a korábbi konfigurációkkal való kompatibilitás érdekében).
- A meglévő `fallbackMAC` működésével való kompatibilitás megőrzése érdekében a `mac-address` **csak `OSString` típusú értéket támogat**, `xx:xx:xx:xx:xx:xx` formátumban (hexadecimális, a kis- és nagybetűk nem számítanak). `OSData` / `OSNumber` nem támogatott.

### Ajánlott prioritási sorrend

1. `pciDevice->getProperty("mac-address")` — OpenCore által injektált érték, **kényszerített felülírás**
2. Hardveres MAC-cím — ha érvényes
3. `Driver Parameters -> fallbackMAC` — ha érvényes
4. `BACKUP_ADDR0_8125/BACKUP_ADDR1_8125` — ha érvényes
5. Véletlenszerű MAC-cím — lokálisan kezelt cím (Locally Administered Address)

## 5. Használható-e a `DeviceProperties Delete` közvetlenül az `IOMACAddress` módosítására?

Következtetés: **ezt a megoldást nem javasolt alkalmazni**; stabil megoldásként nem érdemes erre támaszkodni, mert általában nem garantálható, hogy a MAC-cím ténylegesen a hálózati rétegben is megváltozik.

- A `DeviceProperties Add/Delete` egy adott eszközcsomópont tulajdonságait módosítja; ez nem azonos azzal, hogy a driver végül ezt az értéket használja és beírja a hardverbe.
- Ez a driver az inicializálás során a saját folyamatát követi: kiolvassa a hardverből a MAC-címet, majd meghívja a `rtl8125_rar_set()` függvényt, ezt követően pedig a `currMacAddr` szolgáltatja a címet a rendszer felé.
- Még ha egy adott csomópontban az `IOMACAddress` értéke meg is változik, azt a driver inicializációs folyamata felülírhatja. Így előállhat az a helyzet, hogy **a tulajdonság megváltozott, de a tényleges MAC-cím nem**.

## 6. Ajánlott végleges megoldás

- A legstabilabb megoldás: a driverben új `mac-address` injektálási kulcs bevezetése (`OSString`), amely explicit módon kiolvassa az `pciDevice` tulajdonságát, majd végrehajtja a `rtl8125_rar_set()` hívást.
- Kompatibilitási követelmény: az `Info.plist` `Driver Parameters -> fallbackMAC` visszalépési útvonalát meg kell tartani, hogy a meglévő felhasználói konfigurációk ne sérüljenek.
- Fix MAC-cím kényszerítéséhez a `mac-address` értéknek **elsőbbséget kell élveznie a hardveres értékkel szemben**; a `fallbackMAC` eredeti jelentése változatlan maradjon, vagyis csak akkor vegyen részt, ha a hardveres MAC-cím érvénytelen.

## 7. Build-folyamat és környezeti követelmények — egységes leírás

Ez a rész azt hivatott egységesen tisztázni, hogy különböző hostrendszereken hogyan lehet a projektet az elvárt módon lefordítani úgy, hogy a kompatibilitási tartomány megmaradjon.

### 7.1 Projektkövetelmények — a kompatibilitási tartományt meghatározó fő tényezők

- A target buildbeállításban `MACOSX_DEPLOYMENT_TARGET = 10.15`. Ez azt jelenti, hogy a projekt minimális célzott verziója jelenleg **macOS 10.15+**.
- `SDKROOT = macosx`, vagyis a kiválasztott Xcode által biztosított aktuális macOS SDK használata történik.
- A projekt a `MacKernelSDK/Headers` könyvtárat és a hozzá tartozó `libkmod.a` fájlt használja a régebbi kernel-headerek és a build-kompatibilitás fenntartása érdekében.
- Ezért a hostrendszer verziója **önmagában nem változtatja meg a minimális kompatibilitási célt**, kivéve, ha módosítod a deployment targetet, vagy a célverzión túlmutató szimbólumokat vezetsz be.

### 7.2 Hostrendszer és Xcode követelményei

- macOS 15 (Sequoia) hoston olyan Xcode-verziót kell használni, amely normálisan fut az adott rendszeren — jellemzően Xcode 16.x-et.
- Az Xcode 15.2 a macOS 14 korszakának toolchainje; a benne található legmagasabb macOS SDK a 14.2 SDK.
- Ennél a projektnél, ha macOS 15 alatt végzed a buildet, az **Xcode 16.x használata a megfelelő kombináció**; a kompatibilitási határt továbbra is a projekt deployment targetje határozza meg.

### 7.3 Standard buildlépések — ajánlott fix folyamat

**1. A célként használt Xcode kiválasztása és beállítása:**

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
```

**2. Az aktuális toolchain ellenőrzése:**

```bash
xcodebuild -version
xcode-select -p
clang --version
```

**3. Build Release konfigurációval** — csak fordítás, telepítés nélkül:

```bash
cd /Users/tzraeq/workspace/kext/LucyRTL8125Ethernet
xcodebuild -project LucyRTL8125Ethernet.xcodeproj -target LucyRTL8125Ethernet -configuration Release -sdk macosx CODE_SIGNING_ALLOWED=NO build
```

### 7.4 A buildelt eredmény konzisztenciájának ellenőrzése — kötelező

- Elsődlegesen a buildbeállításokkal ellenőrizd a célverziót, mert ennél a projektnél ez a legmegbízhatóbb módszer:

```bash
xcodebuild -project LucyRTL8125Ethernet.xcodeproj -target LucyRTL8125Ethernet -configuration Release -showBuildSettings | rg "^\s*(MACOSX_DEPLOYMENT_TARGET|SDKROOT|ARCHS|KERNEL_EXTENSION_HEADER_SEARCH_PATHS|KERNEL_FRAMEWORK_HEADERS|WRAPPER_EXTENSION)\s*="
```

- Az architektúra ellenőrzése — a jelenlegi projekt `x86_64`:

```bash
lipo -info build/Release/LucyRTL8125Ethernet.kext/Contents/MacOS/LucyRTL8125Ethernet
```

### 7.5 Hibaelhárítási alapelvek

- Ha az `xcodebuild -version` eredménye nem egyezik az elvárttal, először az `xcode-select` beállítását kell korrigálni.
- Ha a parancssori eszközök verziói összevissza keverednek, elsődleges szempont legyen, hogy az Xcode és a CLT ugyanahhoz a verziórendszerhez tartozzon.
- Ha a buildelt eredmény ellenőrzése nem felel meg az elvárásoknak, először ellenőrizd, hogy nem módosult-e véletlenül a `MACOSX_DEPLOYMENT_TARGET`, `SDKROOT` vagy `ARCHS`.

### 7.6 A környezet felépítése — a telepítéstől a működő buildig

**1. A hostrendszer alapján válaszd ki a használható Xcode főverziót:**

- macOS 14: használható az Xcode 15.2/15.4 (a 15.2-höz 14.2 SDK tartozik).
- macOS 15: elsődlegesen Xcode 16.x ajánlott.

**2. Töltsd le és telepítsd az Xcode-ot:**

- A megfelelő `.xip` fájlt az [Apple Developer Downloads](https://developer.apple.com/download/all/) oldalról lehet letölteni.
- Kicsomagolás után helyezd az `/Applications` könyvtárba. Párhuzamos használat esetén például `Xcode_16.x.app` vagy `Xcode_15.2.app` névre is átnevezhető.

**3. A parancssori fejlesztői könyvtár beállítása és a licenc elfogadása:**

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
```

**4. A Command Line Tools (CLT) telepítése vagy verziójának összehangolása:**

- **A módszer — ajánlott:** első alkalommal indítsd el a célként használt Xcode-ot, majd várd meg, amíg a szükséges komponensek telepítése befejeződik.
- **B módszer — manuális:** telepítsd a megfelelő verziójú `Command Line Tools for Xcode` csomagot.

**5. Végső ellenőrzés — kötelezően sikeresnek kell lennie:**

```bash
xcodebuild -version
xcode-select -p
pkgutil --pkg-info=com.apple.pkg.CLTools_Executables
```

**6. Ezután hajtsd végre a „Standard buildlépések” folyamatot**, vagyis a fordítást és a buildelt eredmény ellenőrzését a 7.3 és 7.4 pont alapján.
