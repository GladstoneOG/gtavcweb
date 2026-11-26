(() => {
  'use strict';

  // ============================================================================
  // Protocol Constants (match cloudflare_worker.js)
  // ============================================================================
  const FRAME_DATA = 1;
  const FRAME_PEER_CONNECTED = 2;
  const FRAME_PEER_DISCONNECTED = 3;
  const FRAME_KICK_PEER = 4;
  const FRAME_ROOM_INFO = 5;
  const FRAME_HOST_ASSIGNED = 6;
  const FRAME_REJECTED = 7;
  const FRAME_WEBRTC_SIGNAL = 8;

  const CH_FAST = 0;
  const CH_RELIABLE = 1;
  const CH_META = 2;

  const MAX_NET_BUF = 65536;
  const HUD_BUF_SIZE = 16384;
  const SETTINGS_BUF_SIZE = 8192;
  const TEMP_STR_BUF_SIZE = 512;

  const RTC_CONFIG = {
    iceServers: [
      { urls: ['stun:stun.l.google.com:19302', 'stun:stun1.l.google.com:19302'] },
    ],
    iceCandidatePoolSize: 4,
  };

  const LS_TOKEN_KEY = 'gtavc_auth_token';
  const LS_NAME_KEY = 'gtavc_player_name';
  const LS_SKIN_KEY = 'gtavc_player_skin';
  const LS_RELAY_KEY = 'gtavc_relay_url';
  const DEFAULT_RELAY_URL = 'https://gtavcweb.johansans999.workers.dev';
  const DEFAULT_ROOM_PORT = 7778;
  const PAGER_IDLE_HIDE_MS = 4500;

  const utf8Encoder = new TextEncoder();
  const utf8Decoder = new TextDecoder('utf-8');

  // ============================================================================
  // Authentic Vice City Catalogs (from retail Vice City data/default.ide)
  // ============================================================================
  const CHARACTER_MODELS = [
    // Story & Specials (0, 109..129)
    { id: 0, name: 'Tommy Vercetti (Protagonist)', group: 'Story & Specials' },
    { id: 109, name: 'Special 01 (Story Cast)', group: 'Story & Specials' },
    { id: 110, name: 'Special 02 (Story Cast)', group: 'Story & Specials' },
    { id: 111, name: 'Special 03 (Story Cast)', group: 'Story & Specials' },
    { id: 112, name: 'Special 04 (Story Cast)', group: 'Story & Specials' },
    { id: 113, name: 'Special 05 (Story Cast)', group: 'Story & Specials' },
    { id: 114, name: 'Special 06 (Story Cast)', group: 'Story & Specials' },
    { id: 115, name: 'Special 07 (Story Cast)', group: 'Story & Specials' },
    { id: 116, name: 'Special 08 (Story Cast)', group: 'Story & Specials' },
    { id: 117, name: 'Special 09 (Story Cast)', group: 'Story & Specials' },
    { id: 118, name: 'Special 10 (Story Cast)', group: 'Story & Specials' },
    { id: 119, name: 'Special 11 (Story Cast)', group: 'Story & Specials' },
    { id: 120, name: 'Special 12 (Story Cast)', group: 'Story & Specials' },
    { id: 121, name: 'Special 13 (Story Cast)', group: 'Story & Specials' },
    { id: 122, name: 'Special 14 (Story Cast)', group: 'Story & Specials' },
    { id: 123, name: 'Special 15 (Story Cast)', group: 'Story & Specials' },
    { id: 124, name: 'Special 16 (Story Cast)', group: 'Story & Specials' },
    { id: 125, name: 'Special 17 (Story Cast)', group: 'Story & Specials' },
    { id: 126, name: 'Special 18 (Story Cast)', group: 'Story & Specials' },
    { id: 127, name: 'Special 19 (Story Cast)', group: 'Story & Specials' },
    { id: 128, name: 'Special 20 (Story Cast)', group: 'Story & Specials' },
    { id: 129, name: 'Special 21 (Story Cast)', group: 'Story & Specials' },

    // Law & Emergency (1..6, 97..104)
    { id: 1, name: 'VCPD Officer', group: 'Law & Emergency' },
    { id: 2, name: 'SWAT Tactical Unit', group: 'Law & Emergency' },
    { id: 3, name: 'FBI Special Agent', group: 'Law & Emergency' },
    { id: 4, name: 'US Army Soldier', group: 'Law & Emergency' },
    { id: 5, name: 'Vice Paramedic', group: 'Law & Emergency' },
    { id: 6, name: 'Vice Fireman', group: 'Law & Emergency' },
    { id: 97, name: 'Vice Squad Undercover 01 (vice1)', group: 'Law & Emergency' },
    { id: 98, name: 'Vice Squad Undercover 02 (vice2)', group: 'Law & Emergency' },
    { id: 99, name: 'Vice Squad Undercover 03 (vice3)', group: 'Law & Emergency' },
    { id: 100, name: 'Vice Squad Undercover 04 (vice4)', group: 'Law & Emergency' },
    { id: 101, name: 'Vice Squad Undercover 05 (vice5)', group: 'Law & Emergency' },
    { id: 102, name: 'Vice Squad Undercover 06 (vice6)', group: 'Law & Emergency' },
    { id: 103, name: 'Vice Squad Undercover 07 (vice7)', group: 'Law & Emergency' },
    { id: 104, name: 'Vice Squad Undercover 08 (vice8)', group: 'Law & Emergency' },

    // Vice City Gangs (83..96)
    { id: 83, name: 'Cuban Gang A (CBa)', group: 'Vice City Gangs' },
    { id: 84, name: 'Cuban Gang B (CBb)', group: 'Vice City Gangs' },
    { id: 85, name: 'Haitian Gang A (HNa)', group: 'Vice City Gangs' },
    { id: 86, name: 'Haitian Gang B (HNb)', group: 'Vice City Gangs' },
    { id: 87, name: 'Streetwannabes / Sharks A (SGa)', group: 'Vice City Gangs' },
    { id: 88, name: 'Streetwannabes / Sharks B (SGb)', group: 'Vice City Gangs' },
    { id: 89, name: 'Diaz Gang A (CLa)', group: 'Vice City Gangs' },
    { id: 90, name: 'Diaz Gang B (CLb)', group: 'Vice City Gangs' },
    { id: 91, name: 'Golf Course Guard A (GDa)', group: 'Vice City Gangs' },
    { id: 92, name: 'Golf Course Guard B (GDb)', group: 'Vice City Gangs' },
    { id: 93, name: 'Vice Biker A (BKa)', group: 'Vice City Gangs' },
    { id: 94, name: 'Vice Biker B (BKb)', group: 'Vice City Gangs' },
    { id: 95, name: 'Vercetti / Patrol Invest A (PGa)', group: 'Vice City Gangs' },
    { id: 96, name: 'Vercetti / Patrol Invest B (PGb)', group: 'Vice City Gangs' },

    // Civilians & Beachgoers (7, 9..82, 105..106)
    { id: 7, name: 'Vice Male Citizen (male01)', group: 'Civilians & Beachgoers' },
    { id: 9, name: 'Ocean Beach Girl A (HFYST)', group: 'Civilians & Beachgoers' },
    { id: 10, name: 'Little Havana Lady (HFOST)', group: 'Civilians & Beachgoers' },
    { id: 11, name: 'Havana Street Guy (HMYST)', group: 'Civilians & Beachgoers' },
    { id: 12, name: 'Havana Elder (HMOST)', group: 'Civilians & Beachgoers' },
    { id: 13, name: 'Starfish Socialite (HFYRI)', group: 'Civilians & Beachgoers' },
    { id: 14, name: 'Affluent Matron (HFORI)', group: 'Civilians & Beachgoers' },
    { id: 15, name: 'Miami Playboy (HMYRI)', group: 'Civilians & Beachgoers' },
    { id: 16, name: 'Vice Executive (HMORI)', group: 'Civilians & Beachgoers' },
    { id: 17, name: 'Ocean Beach Sunbather (HFYBE)', group: 'Civilians & Beachgoers' },
    { id: 18, name: 'Beachfront Tourist F (HFOBE)', group: 'Civilians & Beachgoers' },
    { id: 19, name: 'Beach Surfer Guy (HMYBE)', group: 'Civilians & Beachgoers' },
    { id: 20, name: 'Beachfront Tourist M (HMOBE)', group: 'Civilians & Beachgoers' },
    { id: 21, name: 'Downtown Businesswoman (HFYBU)', group: 'Civilians & Beachgoers' },
    { id: 22, name: 'Malibu Club Girl (HFYMD)', group: 'Civilians & Beachgoers' },
    { id: 23, name: 'Malibu Club Regular (HFYCG)', group: 'Civilians & Beachgoers' },
    { id: 24, name: 'Ocean Drive Nightlife A (HFYPR)', group: 'Civilians & Beachgoers' },
    { id: 25, name: 'Vice Tourist Female A (HFOTR)', group: 'Civilians & Beachgoers' },
    { id: 26, name: 'Vice Tourist Male A (HMOTR)', group: 'Civilians & Beachgoers' },
    { id: 27, name: 'Escobar Airport Staff (HMYAP)', group: 'Civilians & Beachgoers' },
    { id: 28, name: 'Vice Cab Driver A (HMOCA)', group: 'Civilians & Beachgoers' },
    { id: 29, name: 'Vice Harbor Docker (BMODK)', group: 'Civilians & Beachgoers' },
    { id: 30, name: 'Little Haiti Hustler (BMYCR)', group: 'Civilians & Beachgoers' },
    { id: 31, name: 'Street Local Female A (BFYST)', group: 'Civilians & Beachgoers' },
    { id: 32, name: 'Street Local Matron (BFOST)', group: 'Civilians & Beachgoers' },
    { id: 33, name: 'Street Local Male A (BMYST)', group: 'Civilians & Beachgoers' },
    { id: 34, name: 'Downtown Elder (BMOST)', group: 'Civilians & Beachgoers' },
    { id: 35, name: 'Vice High-Roller F (BFYRI)', group: 'Civilians & Beachgoers' },
    { id: 36, name: 'Boutique Patron (BFORI)', group: 'Civilians & Beachgoers' },
    { id: 37, name: 'Vice High-Roller M (BMYRI)', group: 'Civilians & Beachgoers' },
    { id: 38, name: 'Washington Beach Girl (BFYBE)', group: 'Civilians & Beachgoers' },
    { id: 39, name: 'Washington Beach Guy (BMYBE)', group: 'Civilians & Beachgoers' },
    { id: 40, name: 'Boardwalk Tourist F (BFOBE)', group: 'Civilians & Beachgoers' },
    { id: 41, name: 'Boardwalk Tourist M (BMOBE)', group: 'Civilians & Beachgoers' },
    { id: 42, name: 'Vice Financier (BMYBU)', group: 'Civilians & Beachgoers' },
    { id: 43, name: 'Ocean Drive Nightlife B (BFYPR)', group: 'Civilians & Beachgoers' },
    { id: 44, name: 'Vice Tourist Female B (BFOTR)', group: 'Civilians & Beachgoers' },
    { id: 45, name: 'Vice Tourist Male B (BMOTR)', group: 'Civilians & Beachgoers' },
    { id: 46, name: 'Washington Beach Pimp (BMYPI)', group: 'Civilians & Beachgoers' },
    { id: 47, name: 'Beach Bodyguard (BMYBB)', group: 'Civilians & Beachgoers' },
    { id: 48, name: 'Vice Street Punk (WMYCR)', group: 'Civilians & Beachgoers' },
    { id: 49, name: 'Vice Point Girl (WFYST)', group: 'Civilians & Beachgoers' },
    { id: 50, name: 'Vice Point Lady (WFOST)', group: 'Civilians & Beachgoers' },
    { id: 51, name: 'Vice Point Local (WMYST)', group: 'Civilians & Beachgoers' },
    { id: 52, name: 'Vice Point Elder (WMOST)', group: 'Civilians & Beachgoers' },
    { id: 53, name: 'Prawn Island Socialite (WFYRI)', group: 'Civilians & Beachgoers' },
    { id: 54, name: 'Leaf Links Patron (WFORI)', group: 'Civilians & Beachgoers' },
    { id: 55, name: 'Miami Yachter (WMYRI)', group: 'Civilians & Beachgoers' },
    { id: 56, name: 'Country Club Member (WMORI)', group: 'Civilians & Beachgoers' },
    { id: 57, name: 'Vice Beach Girl (WFYBE)', group: 'Civilians & Beachgoers' },
    { id: 58, name: 'Vice Beach Guy (WMYBE)', group: 'Civilians & Beachgoers' },
    { id: 59, name: 'Coastal Tourist F (WFOBE)', group: 'Civilians & Beachgoers' },
    { id: 60, name: 'Coastal Tourist M (WMOBE)', group: 'Civilians & Beachgoers' },
    { id: 61, name: 'Construction Worker (WMYCW)', group: 'Civilians & Beachgoers' },
    { id: 62, name: 'Leaf Links Golfer M (WMYGO)', group: 'Civilians & Beachgoers' },
    { id: 63, name: 'Leaf Links Golfer F (WFOGO)', group: 'Civilians & Beachgoers' },
    { id: 64, name: 'Leaf Links Senior Golfer (WMOGO)', group: 'Civilians & Beachgoers' },
    { id: 65, name: 'Beach Lifeguard F (WFYLG)', group: 'Civilians & Beachgoers' },
    { id: 66, name: 'Beach Lifeguard M (WMYLG)', group: 'Civilians & Beachgoers' },
    { id: 67, name: 'Corporate Exec F (WFYBU)', group: 'Civilians & Beachgoers' },
    { id: 68, name: 'Corporate Exec M (WMYBU)', group: 'Civilians & Beachgoers' },
    { id: 69, name: 'Downtown Broker (WMOBU)', group: 'Civilians & Beachgoers' },
    { id: 70, name: 'Ocean Drive Nightlife C (WFYPR)', group: 'Civilians & Beachgoers' },
    { id: 71, name: 'Vice Tourist Female C (WFOTR)', group: 'Civilians & Beachgoers' },
    { id: 72, name: 'Vice Tourist Male C (WMOTR)', group: 'Civilians & Beachgoers' },
    { id: 73, name: 'Malibu Hustler (WMYPI)', group: 'Civilians & Beachgoers' },
    { id: 74, name: 'Vice Cab Driver B (WMOCA)', group: 'Civilians & Beachgoers' },
    { id: 75, name: 'Ocean Drive Jogger F (WFYJG)', group: 'Civilians & Beachgoers' },
    { id: 76, name: 'Ocean Drive Jogger M (WMYJG)', group: 'Civilians & Beachgoers' },
    { id: 77, name: 'Ocean Beach Skater F (WFYSK)', group: 'Civilians & Beachgoers' },
    { id: 78, name: 'Ocean Beach Skater M (WMYSK)', group: 'Civilians & Beachgoers' },
    { id: 79, name: 'North Point Mall Shopper A (WFYSH)', group: 'Civilians & Beachgoers' },
    { id: 80, name: 'North Point Mall Shopper B (WFOSH)', group: 'Civilians & Beachgoers' },
    { id: 81, name: 'Vice Camera Tourist F (JFOTO)', group: 'Civilians & Beachgoers' },
    { id: 82, name: 'Vice Camera Tourist M (JMOTO)', group: 'Civilians & Beachgoers' },
    { id: 105, name: 'Malibu / Pole Position Dancer A (WFYG1)', group: 'Civilians & Beachgoers' },
    { id: 106, name: 'Malibu / Pole Position Dancer B (WFYG2)', group: 'Civilians & Beachgoers' },
  ];

  // Exact vehicle model indices from Vice City retail default.ide (130..236)
  const VEHICLES = [
    // Motorcycles
    { id: 191, name: 'PCJ-600', group: 'Motorcycles' },
    { id: 192, name: 'Faggio', group: 'Motorcycles' },
    { id: 198, name: 'Sanchez', group: 'Motorcycles' },
    { id: 193, name: 'Freeway', group: 'Motorcycles' },
    { id: 166, name: 'Angel', group: 'Motorcycles' },
    { id: 178, name: 'Pizza Boy', group: 'Motorcycles' },

    // Helicopters & Planes
    { id: 155, name: 'Hunter (Apache)', group: 'Helicopters & Planes' },
    { id: 217, name: 'Maverick', group: 'Helicopters & Planes' },
    { id: 218, name: 'VCN Maverick', group: 'Helicopters & Planes' },
    { id: 227, name: 'Police Maverick', group: 'Helicopters & Planes' },
    { id: 177, name: 'Sea Sparrow', group: 'Helicopters & Planes' },
    { id: 199, name: 'Sparrow', group: 'Helicopters & Planes' },
    { id: 165, name: 'Chopper', group: 'Helicopters & Planes' },
    { id: 190, name: 'Skimmer (Seaplane)', group: 'Helicopters & Planes' },
    { id: 180, name: 'Airtrain', group: 'Helicopters & Planes' },
    { id: 181, name: 'DeadDodo', group: 'Helicopters & Planes' },
    { id: 194, name: 'RC Baron', group: 'Helicopters & Planes' },
    { id: 195, name: 'RC Raider', group: 'Helicopters & Planes' },
    { id: 231, name: 'RC Goblin', group: 'Helicopters & Planes' },

    // Sports & Exotics
    { id: 145, name: 'Cheetah', group: 'Sports & Exotics' },
    { id: 236, name: 'Vice Squad Cheetah', group: 'Sports & Exotics' },
    { id: 141, name: 'Infernus', group: 'Sports & Exotics' },
    { id: 159, name: 'Banshee', group: 'Sports & Exotics' },
    { id: 132, name: 'Stinger', group: 'Sports & Exotics' },
    { id: 210, name: 'Comet', group: 'Sports & Exotics' },
    { id: 207, name: 'Phoenix', group: 'Sports & Exotics' },
    { id: 211, name: 'Deluxo', group: 'Sports & Exotics' },
    { id: 206, name: 'Sabre Turbo', group: 'Sports & Exotics' },
    { id: 154, name: 'BF Injection', group: 'Sports & Exotics' },
    { id: 224, name: 'Hotring Racer', group: 'Sports & Exotics' },
    { id: 232, name: 'Hotring A', group: 'Sports & Exotics' },
    { id: 233, name: 'Hotring B', group: 'Sports & Exotics' },
    { id: 225, name: 'Sandking', group: 'Sports & Exotics' },
    { id: 187, name: 'Caddy (Golf Cart)', group: 'Sports & Exotics' },

    // Emergency & Military
    { id: 162, name: 'Rhino (Tank)', group: 'Emergency & Military' },
    { id: 156, name: 'VCPD Cruiser', group: 'Emergency & Military' },
    { id: 147, name: 'FBI Washington', group: 'Emergency & Military' },
    { id: 220, name: 'FBI Rancher', group: 'Emergency & Military' },
    { id: 157, name: 'Enforcer', group: 'Emergency & Military' },
    { id: 163, name: 'Barracks OL', group: 'Emergency & Military' },
    { id: 146, name: 'Ambulance', group: 'Emergency & Military' },
    { id: 137, name: 'Fire Truck', group: 'Emergency & Military' },
    { id: 158, name: 'Securicar', group: 'Emergency & Military' },

    // Sedans, Coupes & Gang
    { id: 135, name: 'Sentinel', group: 'Sedans, Coupes & Gang' },
    { id: 174, name: 'Sentinel XS', group: 'Sedans, Coupes & Gang' },
    { id: 175, name: 'Admiral', group: 'Sedans, Coupes & Gang' },
    { id: 151, name: 'Washington', group: 'Sedans, Coupes & Gang' },
    { id: 142, name: 'Voodoo', group: 'Sedans, Coupes & Gang' },
    { id: 164, name: 'Cuban Hermes', group: 'Sedans, Coupes & Gang' },
    { id: 204, name: 'Hermes', group: 'Sedans, Coupes & Gang' },
    { id: 169, name: 'Stallion', group: 'Sedans, Coupes & Gang' },
    { id: 205, name: 'Sabre', group: 'Sedans, Coupes & Gang' },
    { id: 196, name: 'Glendale', group: 'Sedans, Coupes & Gang' },
    { id: 197, name: 'Oceanic', group: 'Sedans, Coupes & Gang' },
    { id: 139, name: 'Stretch', group: 'Sedans, Coupes & Gang' },
    { id: 201, name: 'Love Fist Limo', group: 'Sedans, Coupes & Gang' },
    { id: 150, name: 'Taxi', group: 'Sedans, Coupes & Gang' },
    { id: 168, name: 'Cabbie', group: 'Sedans, Coupes & Gang' },
    { id: 188, name: 'Zebra Cab', group: 'Sedans, Coupes & Gang' },
    { id: 216, name: 'Kaufman Cab', group: 'Sedans, Coupes & Gang' },
    { id: 226, name: 'Blista Compact', group: 'Sedans, Coupes & Gang' },
    { id: 130, name: 'Landstalker', group: 'Sedans, Coupes & Gang' },
    { id: 131, name: 'Idaho', group: 'Sedans, Coupes & Gang' },
    { id: 134, name: 'Perennial', group: 'Sedans, Coupes & Gang' },
    { id: 140, name: 'Manana', group: 'Sedans, Coupes & Gang' },
    { id: 148, name: 'Moonbeam', group: 'Sedans, Coupes & Gang' },
    { id: 149, name: 'Esperanto', group: 'Sedans, Coupes & Gang' },
    { id: 219, name: 'Rancher', group: 'Sedans, Coupes & Gang' },
    { id: 221, name: 'Virgo', group: 'Sedans, Coupes & Gang' },
    { id: 222, name: 'Greenwood', group: 'Sedans, Coupes & Gang' },
    { id: 230, name: 'Mesa Grande', group: 'Sedans, Coupes & Gang' },
    { id: 234, name: 'Bloodring A', group: 'Sedans, Coupes & Gang' },
    { id: 235, name: 'Bloodring B', group: 'Sedans, Coupes & Gang' },

    // Trucks, Vans & RC
    { id: 171, name: 'RC Bandit', group: 'Trucks, Vans & RC' },
    { id: 143, name: 'Pony', group: 'Trucks, Vans & RC' },
    { id: 144, name: 'Mule', group: 'Trucks, Vans & RC' },
    { id: 152, name: 'Bobcat', group: 'Trucks, Vans & RC' },
    { id: 153, name: 'Mr. Whoopee', group: 'Trucks, Vans & RC' },
    { id: 170, name: 'Rumpo', group: 'Trucks, Vans & RC' },
    { id: 172, name: "Romero's Hearse", group: 'Trucks, Vans & RC' },
    { id: 173, name: 'Packer', group: 'Trucks, Vans & RC' },
    { id: 179, name: 'Gang Burrito', group: 'Trucks, Vans & RC' },
    { id: 185, name: 'Flatbed', group: 'Trucks, Vans & RC' },
    { id: 186, name: 'Yankee', group: 'Trucks, Vans & RC' },
    { id: 189, name: 'Top Fun', group: 'Trucks, Vans & RC' },
    { id: 200, name: 'Patriot', group: 'Trucks, Vans & RC' },
    { id: 208, name: 'Walton', group: 'Trucks, Vans & RC' },
    { id: 209, name: 'Regina', group: 'Trucks, Vans & RC' },
    { id: 212, name: 'Burrito', group: 'Trucks, Vans & RC' },
    { id: 213, name: 'Spand Express', group: 'Trucks, Vans & RC' },
    { id: 215, name: 'Baggage Handler', group: 'Trucks, Vans & RC' },
    { id: 228, name: 'Boxville', group: 'Trucks, Vans & RC' },
    { id: 229, name: 'Benson', group: 'Trucks, Vans & RC' },
    { id: 133, name: 'Linerunner', group: 'Trucks, Vans & RC' },
    { id: 138, name: 'Trashmaster', group: 'Trucks, Vans & RC' },
    { id: 161, name: 'Bus', group: 'Trucks, Vans & RC' },
    { id: 167, name: 'Coach', group: 'Trucks, Vans & RC' },

    // Boats
    { id: 176, name: 'Squalo', group: 'Boats' },
    { id: 182, name: 'Speeder', group: 'Boats' },
    { id: 223, name: 'Jetmax', group: 'Boats' },
    { id: 160, name: 'Predator', group: 'Boats' },
    { id: 136, name: 'Rio', group: 'Boats' },
    { id: 183, name: 'Reefer', group: 'Boats' },
    { id: 184, name: 'Tropic', group: 'Boats' },
    { id: 202, name: 'Coastguard', group: 'Boats' },
    { id: 203, name: 'Dinghy', group: 'Boats' },
    { id: 214, name: 'Marquis', group: 'Boats' },
  ];

  // Exact eWeaponType enum values from WeaponType.h (1..36)
  const PICKUP_WEAPONS = [
    { id: 1, name: 'Brass Knuckles' },
    { id: 2, name: 'Screwdriver' },
    { id: 3, name: 'Golf Club' },
    { id: 4, name: 'Nightstick' },
    { id: 5, name: 'Knife' },
    { id: 6, name: 'Baseball Bat' },
    { id: 7, name: 'Hammer' },
    { id: 8, name: 'Meat Cleaver' },
    { id: 9, name: 'Machete' },
    { id: 10, name: 'Katana' },
    { id: 11, name: 'Chainsaw' },
    { id: 12, name: 'Grenade' },
    { id: 13, name: 'Remote Grenade' },
    { id: 14, name: 'Tear Gas' },
    { id: 15, name: 'Molotov' },
    { id: 17, name: 'Colt .45' },
    { id: 18, name: 'Python .357' },
    { id: 19, name: 'Chrome Shotgun' },
    { id: 20, name: 'SPAS-12' },
    { id: 21, name: 'Stubby Shotgun' },
    { id: 22, name: 'Tec-9' },
    { id: 23, name: 'Uzi' },
    { id: 24, name: 'Ingram Mac-10' },
    { id: 25, name: 'MP5' },
    { id: 26, name: 'M4' },
    { id: 27, name: 'Kruger' },
    { id: 28, name: 'Sniper Rifle' },
    { id: 29, name: 'Laser Sniper' },
    { id: 30, name: 'Rocket Launcher' },
    { id: 31, name: 'Flamethrower' },
    { id: 32, name: 'M60' },
    { id: 33, name: 'Minigun' },
    { id: 36, name: 'Camera' },
  ];

  const RADIO_STATIONS = [
    { id: 0, name: 'Wildstyle' },
    { id: 1, name: 'Flash FM' },
    { id: 2, name: 'K-Chat' },
    { id: 3, name: 'Fever 105' },
    { id: 4, name: 'V-Rock' },
    { id: 5, name: 'VCPR' },
    { id: 6, name: 'Radio Espantoso' },
    { id: 7, name: 'Emotion 98.3' },
    { id: 8, name: 'Wave 103' },
    { id: 9, name: 'MP3 Player / Off' },
  ];

  // ============================================================================
  // Utility Helpers
  // ============================================================================
  function clampInt(val, min, max, fallback) {
    const n = Number.parseInt(String(val ?? ''), 10);
    if (!Number.isFinite(n)) return fallback;
    return Math.max(min, Math.min(max, n));
  }

  function sanitizeName(raw, fallback = 'Tommy') {
    const clean = String(raw ?? '').replace(/[\r\n\t<>]/g, ' ').trim().slice(0, 24);
    return clean || fallback;
  }

  function formatBytes(bytes) {
    if (!Number.isFinite(bytes) || bytes <= 0) return '0 B';
    if (bytes < 1024) return `${bytes} B`;
    const mib = bytes / (1024 * 1024);
    if (mib >= 1) return `${mib.toFixed(1)} MiB`;
    return `${(bytes / 1024).toFixed(0)} KiB`;
  }

  function formatTimestamp(ms) {
    if (!ms) return '';
    try {
      return new Date(ms).toLocaleString(undefined, {
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
      });
    } catch (_) {
      return '';
    }
  }

  function escapeHtml(text) {
    return String(text ?? '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function makeFrame(type, peerId, channel, payloadBytes) {
    const payload = payloadBytes
      ? payloadBytes instanceof Uint8Array
        ? payloadBytes
        : new Uint8Array(payloadBytes)
      : new Uint8Array(0);
    const out = new Uint8Array(4 + payload.byteLength);
    out[0] = type & 0xff;
    out[1] = (peerId >> 8) & 0xff;
    out[2] = peerId & 0xff;
    out[3] = channel & 0xff;
    if (payload.byteLength > 0) {
      out.set(payload, 4);
    }
    return out;
  }

  function uint8ToBase64(bytes) {
    let binary = '';
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk) {
      binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    }
    return btoa(binary);
  }

  function base64ToUint8(b64) {
    const bin = atob(b64);
    const len = bin.length;
    const out = new Uint8Array(len);
    for (let i = 0; i < len; i++) {
      out[i] = bin.charCodeAt(i);
    }
    return out;
  }

  async function compressBytesToGzipB64(uint8) {
    if (typeof CompressionStream === 'function') {
      const stream = new Blob([uint8]).stream().pipeThrough(new CompressionStream('gzip'));
      const buf = await new Response(stream).arrayBuffer();
      return uint8ToBase64(new Uint8Array(buf));
    }
    return uint8ToBase64(uint8);
  }

  async function decompressGzipB64ToBytes(b64) {
    const raw = base64ToUint8(b64);
    if (raw.byteLength >= 2 && raw[0] === 0x1f && raw[1] === 0x8b && typeof DecompressionStream === 'function') {
      const stream = new Blob([raw]).stream().pipeThrough(new DecompressionStream('gzip'));
      const buf = await new Response(stream).arrayBuffer();
      return new Uint8Array(buf);
    }
    return raw;
  }

  function getSkinLabel(skinId) {
    const found = CHARACTER_MODELS.find((m) => m.id === Number(skinId));
    return found ? found.name : `Ped #${skinId}`;
  }

  function isEditableTarget(el) {
    if (!el) return false;
    const tag = el.tagName;
    return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || Boolean(el.isContentEditable);
  }

  function normalizeRelayUrl(raw) {
    let clean = String(raw ?? '').trim().replace(/\/+$/, '');
    if (clean.endsWith('/ws')) clean = clean.slice(0, -3);
    return clean;
  }

  function resolveInitialRelayUrl() {
    try {
      const urlRelay = new URLSearchParams(location.search).get('relay');
      if (urlRelay && urlRelay.trim()) {
        const clean = normalizeRelayUrl(urlRelay);
        localStorage.setItem(LS_RELAY_KEY, clean);
        return clean;
      }
      const saved = localStorage.getItem(LS_RELAY_KEY);
      if (saved && saved.trim()) {
        return normalizeRelayUrl(saved);
      }
    } catch (_) {}
    if (isLocalHostOrigin() && typeof location !== 'undefined' && location.origin) {
      return normalizeRelayUrl(location.origin);
    }
    const globalCfg =
      (typeof window !== 'undefined' && (window.GTAVC_RELAY_URL || window.GTAVC_DEFAULT_RELAY)) || '';
    return normalizeRelayUrl(globalCfg) || DEFAULT_RELAY_URL;
  }

  function resolveUrlRoomPort() {
    try {
      const raw = new URLSearchParams(location.search).get('room');
      if (!raw) return null;
      const n = Number.parseInt(raw, 10);
      if (Number.isFinite(n) && n >= 1024 && n <= 65535) return n;
    } catch (_) {}
    return null;
  }

  function isLocalHostOrigin() {
    return (
      typeof location !== 'undefined' &&
      (location.hostname === 'localhost' ||
        location.hostname === '127.0.0.1' ||
        location.hostname === '::1')
    );
  }

  // ============================================================================
  // Application State
  // ============================================================================
  const initialRoomFromUrl = resolveUrlRoomPort();
  const initialLocalName = sanitizeName(
    typeof localStorage !== 'undefined' ? localStorage.getItem(LS_NAME_KEY) : '',
    'Tommy'
  );

  const state = {
    booted: false,
    booting: false,
    menuOpen: false,
    activeTab: 'vice-net',
    pagerMinimized: false,
    pagerIdleHidden: false,
    pagerHovered: false,
    pointerLocked: false,

    relayUrl: resolveInitialRelayUrl(),
    localRelayFallback: false,

    mode: initialRoomFromUrl ? 'client' : 'offline', // 'offline' | 'host' | 'client'
    localPeerId: 1,
    isHost: !initialRoomFromUrl,
    roomPort: initialRoomFromUrl || DEFAULT_ROOM_PORT,
    roomName: `${initialLocalName}'s Vice City`,
    coopMode: 'story_coop',
    openIslands: false,
    loadCutsOnBoot: false,

    localName: initialLocalName,
    localSkin: clampInt(
      typeof localStorage !== 'undefined' ? localStorage.getItem(LS_SKIN_KEY) : 0,
      0,
      129,
      0
    ),

    authToken: (typeof localStorage !== 'undefined' ? localStorage.getItem(LS_TOKEN_KEY) : '') || '',
    authUser: null,
    campaigns: [],
    rooms: [],
    roomInfo: null,

    hud: null,
    frontendSettings: {
      music_vol: 96,
      sfx_vol: 110,
      radio_station: 0,
      brightness: 256,
      draw_dist: 60,
      subtitles: 1,
      widescreen: 1,
      invert_y: 0,
      control_method: 0,
      show_hud: 1,
    },
    frontendKeyMap: {},

    pagerMessages: [],
    cheatToggles: new Set(),
  };

  // ============================================================================
  // WASM Module & Scratch Buffer Bridge
  // ============================================================================
  const wasm = {
    mod: null,
    runtimeReady: false,
    offlineInited: false,
    engineLiveSyncDone: false,
    buf: null,
  };

  function getMalloc(mod) {
    return mod._malloc || mod.malloc;
  }

  function getFree(mod) {
    return mod._free || mod.free;
  }

  function readCString(mod, ptr) {
    if (!ptr) return '';
    if (typeof mod.UTF8ToString === 'function') {
      return mod.UTF8ToString(ptr);
    }
    const heap = mod.HEAPU8;
    if (!heap) return '';
    let end = ptr;
    while (end < heap.length && heap[end] !== 0) end++;
    return utf8Decoder.decode(heap.subarray(ptr, end));
  }

  function writeCString(mod, str, ptr, maxBytes) {
    if (!ptr || maxBytes <= 0) return;
    const s = String(str ?? '');
    if (typeof mod.stringToUTF8 === 'function') {
      mod.stringToUTF8(s, ptr, maxBytes);
      return;
    }
    const encoded = utf8Encoder.encode(s);
    const copyLen = Math.min(encoded.byteLength, maxBytes - 1);
    mod.HEAPU8.set(encoded.subarray(0, copyLen), ptr);
    mod.HEAPU8[ptr + copyLen] = 0;
  }

  function ensureScratchBuffers(mod) {
    if (wasm.buf) return wasm.buf;
    const malloc = getMalloc(mod);
    if (typeof malloc !== 'function') return null;

    wasm.buf = {
      netOutPtr: malloc(MAX_NET_BUF),
      netInPtr: malloc(MAX_NET_BUF),
      netInCap: MAX_NET_BUF,
      outChannelPtr: malloc(4),
      outTargetPtr: malloc(4),
      outFloatsPtr: malloc(64),
      outIntsPtr: malloc(64),
      hudBufPtr: malloc(HUD_BUF_SIZE),
      settingsBufPtr: malloc(SETTINGS_BUF_SIZE),
      outPtrPtr: malloc(4),
      strPtr: malloc(TEMP_STR_BUF_SIZE),
    };
    return wasm.buf;
  }

  const bridge = {
    isReady() {
      return Boolean(wasm.mod && wasm.runtimeReady);
    },

    mpInit(localPeerId, isHost) {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_init !== 'function') return false;
      mod._mp_init(localPeerId | 0, isHost ? 1 : 0);
      return true;
    },

    mpShutdown() {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_shutdown !== 'function') return;
      mod._mp_shutdown();
    },

    mpSetPlayerInfo(peerId, name, modelIndex) {
      const mod = wasm.mod;
      const buf = wasm.buf;
      if (!mod || !buf || typeof mod._mp_set_player_info !== 'function') return;
      writeCString(mod, sanitizeName(name), buf.strPtr, TEMP_STR_BUF_SIZE);
      mod._mp_set_player_info(peerId | 0, buf.strPtr, modelIndex | 0);
    },

    mpGetLocalPlayerState() {
      const mod = wasm.mod;
      const buf = wasm.buf;
      if (!mod || !buf || typeof mod._mp_get_local_player_state !== 'function') return null;
      mod.HEAPF32.fill(0, buf.outFloatsPtr >> 2, (buf.outFloatsPtr >> 2) + 16);
      mod.HEAP32.fill(0, buf.outIntsPtr >> 2, (buf.outIntsPtr >> 2) + 16);
      mod._mp_get_local_player_state(buf.outFloatsPtr, buf.outIntsPtr);
      return {
        floats: Array.from(mod.HEAPF32.subarray(buf.outFloatsPtr >> 2, (buf.outFloatsPtr >> 2) + 16)),
        ints: Array.from(mod.HEAP32.subarray(buf.outIntsPtr >> 2, (buf.outIntsPtr >> 2) + 16)),
      };
    },

    mpPushNetPacket(fromPeerId, bytes) {
      const mod = wasm.mod;
      const buf = wasm.buf;
      if (!mod || !buf || typeof mod._mp_push_net_packet !== 'function') return;
      const len = bytes.byteLength;
      if (len <= 0) return;
      if (len > buf.netInCap) {
        const malloc = getMalloc(mod);
        const free = getFree(mod);
        if (typeof malloc === 'function') {
          if (typeof free === 'function' && buf.netInPtr) free(buf.netInPtr);
          buf.netInPtr = malloc(len);
          buf.netInCap = len;
        }
      }
      if (len > buf.netInCap) return;
      mod.HEAPU8.set(bytes, buf.netInPtr);
      mod._mp_push_net_packet(fromPeerId | 0, buf.netInPtr, len | 0);
    },

    drainNetPackets(sendFn) {
      const mod = wasm.mod;
      const buf = wasm.buf;
      if (!mod || !buf || typeof mod._mp_poll_net_packet !== 'function') return;

      for (let i = 0; i < 128; i++) {
        const len = mod._mp_poll_net_packet(
          buf.outChannelPtr,
          buf.outTargetPtr,
          buf.netOutPtr,
          MAX_NET_BUF
        );
        if (len <= 0) break;
        const channel = mod.HEAP32[buf.outChannelPtr >> 2];
        const targetPeer = mod.HEAP32[buf.outTargetPtr >> 2];
        const pkt = new Uint8Array(mod.HEAPU8.subarray(buf.netOutPtr, buf.netOutPtr + len));
        sendFn(targetPeer, channel, pkt);
      }
    },

    mpExecCheat(cheatId, param1 = 0, param2 = 0) {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_exec_cheat !== 'function') return false;
      mod._mp_exec_cheat(cheatId | 0, param1 | 0, param2 | 0);
      return true;
    },

    mpSpawnVehicle(modelIndex, aheadDist = 6.0) {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_spawn_vehicle !== 'function') return false;
      mod._mp_spawn_vehicle(modelIndex | 0, Number(aheadDist) || 6.0);
      return true;
    },

    mpSpawnPickup(pickupKind, weaponOrAmount = 0) {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_spawn_pickup !== 'function') return false;
      mod._mp_spawn_pickup(pickupKind | 0, weaponOrAmount | 0);
      return true;
    },

    mpSetOpenIslands(open) {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_set_open_islands !== 'function') return false;
      const modeVal = open === 2 ? 2 : (open ? 1 : 0);
      mod._mp_set_open_islands(modeVal);
      return true;
    },

    mpExportSaveState() {
      const mod = wasm.mod;
      const buf = wasm.buf;
      if (!mod || !buf || typeof mod._mp_export_save_state !== 'function') return null;
      try {
        if (mod.FS && typeof mod.FS.mkdirTree === 'function') mod.FS.mkdirTree('/userfiles');
      } catch (_) {}
      mod.HEAP32[buf.outPtrPtr >> 2] = 0;
      const len = mod._mp_export_save_state(buf.outPtrPtr);
      const dataPtr = (mod.HEAP32[buf.outPtrPtr >> 2]) >>> 0;
      if (!dataPtr || len <= 0) return null;
      const copy = new Uint8Array(mod.HEAPU8.subarray(dataPtr, dataPtr + len));
      if (typeof mod._mp_free_save_state === 'function') {
        mod._mp_free_save_state(dataPtr);
      }
      return copy;
    },

    mpImportSaveState(bytes) {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_import_save_state !== 'function') return false;
      try {
        if (mod.FS && typeof mod.FS.mkdirTree === 'function') mod.FS.mkdirTree('/userfiles');
      } catch (_) {}
      const malloc = getMalloc(mod);
      const free = getFree(mod);
      if (typeof malloc !== 'function') return false;
      const ptr = malloc(bytes.byteLength);
      if (!ptr) return false;
      try {
        mod.HEAPU8.set(bytes, ptr);
        const res = mod._mp_import_save_state(ptr, bytes.byteLength | 0);
        return res !== 0;
      } finally {
        if (typeof free === 'function') free(ptr);
      }
    },

    mpTriggerRestart(newGame) {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_trigger_restart !== 'function') return false;
      mod._mp_trigger_restart(newGame ? 1 : 0);
      return true;
    },

    mpSetUiPauseBlock(block) {
      const mod = wasm.mod;
      if (!mod || typeof mod._mp_set_ui_pause_block !== 'function') return;
      mod._mp_set_ui_pause_block(block ? 1 : 0);
    },

    mpGetHudState() {
      const mod = wasm.mod;
      const buf = wasm.buf;
      if (!mod || !buf || typeof mod._mp_get_hud_state !== 'function') return null;
      mod.HEAPU8[buf.hudBufPtr] = 0;
      mod._mp_get_hud_state(buf.hudBufPtr, HUD_BUF_SIZE);
      const raw = readCString(mod, buf.hudBufPtr);
      if (!raw || raw[0] !== '{') return null;
      try {
        return JSON.parse(raw);
      } catch (_) {
        return null;
      }
    },

    mpSetFrontendSetting(key, val) {
      const mod = wasm.mod;
      const buf = wasm.buf;
      if (!mod || !buf || typeof mod._mp_set_frontend_setting !== 'function') return;
      const mappedKey = state.frontendKeyMap[key] || key;
      writeCString(mod, mappedKey, buf.strPtr, TEMP_STR_BUF_SIZE);
      mod._mp_set_frontend_setting(buf.strPtr, Number(val));
    },

    mpGetFrontendSettings() {
      const mod = wasm.mod;
      const buf = wasm.buf;
      if (!mod || !buf || typeof mod._mp_get_frontend_settings !== 'function') return null;
      mod.HEAPU8[buf.settingsBufPtr] = 0;
      mod._mp_get_frontend_settings(buf.settingsBufPtr, SETTINGS_BUF_SIZE);
      const raw = readCString(mod, buf.settingsBufPtr);
      if (!raw || raw[0] !== '{') return null;
      try {
        return JSON.parse(raw);
      } catch (_) {
        return null;
      }
    },
  };

  // ============================================================================
  // Vice Cellular / Pager Feed & Auto-Hide
  // ============================================================================
  let pagerHideTimer = null;

  function applyPagerVisibilityDOM() {
    const pagerEl = document.getElementById('vice-pager');
    if (!pagerEl) return;
    const hide = Boolean(
      state.booted && state.pagerIdleHidden && !state.menuOpen && !state.pagerHovered
    );
    pagerEl.classList.toggle('pager-idle-hidden', hide);
  }

  function clearPagerAutoHideTimer() {
    if (pagerHideTimer !== null) {
      clearTimeout(pagerHideTimer);
      pagerHideTimer = null;
    }
  }

  function schedulePagerAutoHide(holdMs = PAGER_IDLE_HIDE_MS) {
    clearPagerAutoHideTimer();
    if (!state.booted || state.menuOpen || state.pagerHovered) {
      applyPagerVisibilityDOM();
      return;
    }
    const ms = Math.max(500, Number(holdMs) || PAGER_IDLE_HIDE_MS);
    pagerHideTimer = setTimeout(() => {
      pagerHideTimer = null;
      if (!state.booted || state.menuOpen || state.pagerHovered) {
        applyPagerVisibilityDOM();
        return;
      }
      state.pagerIdleHidden = true;
      applyPagerVisibilityDOM();
    }, ms);
  }

  function wakePager(holdMs = PAGER_IDLE_HIDE_MS) {
    state.pagerIdleHidden = false;
    applyPagerVisibilityDOM();
    schedulePagerAutoHide(holdMs);
  }

  function addPagerMessage(text, tone = 'normal') {
    const now = new Date();
    const hh = String(now.getHours()).padStart(2, '0');
    const mm = String(now.getMinutes()).padStart(2, '0');
    state.pagerMessages.push({ time: `${hh}:${mm}`, text: String(text), tone });
    if (state.pagerMessages.length > 40) {
      state.pagerMessages.shift();
    }
    renderPager();
    if (state.booted) {
      wakePager();
    }
    if (state.menuOpen && state.activeTab === 'vice-net') {
      renderNetChat();
    }
  }

  function getRelayHttpBase(forceLocalOrigin = false) {
    if (forceLocalOrigin || (state.localRelayFallback && isLocalHostOrigin())) {
      return typeof location !== 'undefined' ? location.origin : DEFAULT_RELAY_URL;
    }
    const configured = normalizeRelayUrl(state.relayUrl);
    if (configured) return configured;
    return typeof location !== 'undefined' ? location.origin : DEFAULT_RELAY_URL;
  }

  function getRelayWsUrl(params, forceLocalOrigin = false) {
    const httpBase = getRelayHttpBase(forceLocalOrigin).replace(/\/+$/, '');
    const wsBase = httpBase.replace(/^http/i, 'ws');
    const qs = new URLSearchParams(params);
    return `${wsBase}/ws?${qs.toString()}`;
  }

  // ============================================================================
  // DO WebSocket + WebRTC Mesh Networking Client
  // ============================================================================
  const net = {
    ws: null,
    peers: new Map(),
    connecting: false,
    connected: false,
    discoverWs: null,

    wsUrlFor(params, forceLocalOrigin = false) {
      return getRelayWsUrl(params, forceLocalOrigin);
    },

    disconnect(quiet = false) {
      for (const peer of this.peers.values()) {
        this.cleanupPeer(peer);
      }
      this.peers.clear();

      if (this.ws) {
        const old = this.ws;
        this.ws = null;
        try {
          old.close(1000, 'Client left');
        } catch (_) {}
      }
      this.connecting = false;
      this.connected = false;
      state.roomInfo = null;

      if (!quiet) {
        addPagerMessage('VICE NET: Disconnected (Offline Mode).', 'warn');
      }
      renderPager();
      renderViceNetPanel();
    },

    cleanupPeer(peer) {
      try { if (peer.dcFast) peer.dcFast.close(); } catch (_) {}
      try { if (peer.dcReliable) peer.dcReliable.close(); } catch (_) {}
      try { if (peer.dcMeta) peer.dcMeta.close(); } catch (_) {}
      try { if (peer.pc) peer.pc.close(); } catch (_) {}
      peer.dcFast = null;
      peer.dcReliable = null;
      peer.dcMeta = null;
      peer.pc = null;
    },

    ensurePeer(peerId, name, skinId = 0) {
      let peer = this.peers.get(peerId);
      if (!peer) {
        peer = {
          peerId,
          name: sanitizeName(name, `Player ${peerId}`),
          skinId: clampInt(skinId, 0, 129, 0),
          pc: null,
          dcFast: null,
          dcReliable: null,
          dcMeta: null,
          pendingCandidates: [],
          rttMs: null,
          pingSentAt: 0,
          transport: 'RELAY',
        };
        this.peers.set(peerId, peer);
      } else {
        if (name) peer.name = sanitizeName(name, peer.name);
        if (Number.isFinite(skinId)) peer.skinId = clampInt(skinId, 0, 129, peer.skinId);
      }
      return peer;
    },

    updatePeerTransportState(peer) {
      const fastOpen = peer.dcFast && peer.dcFast.readyState === 'open';
      const relOpen = peer.dcReliable && peer.dcReliable.readyState === 'open';
      peer.transport = fastOpen || relOpen ? 'P2P' : 'RELAY';
    },

    sendSignal(targetPeerId, payloadObj) {
      if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
      const bytes = utf8Encoder.encode(JSON.stringify(payloadObj));
      this.ws.send(makeFrame(FRAME_WEBRTC_SIGNAL, targetPeerId, 0, bytes));
    },

    attachDataChannel(peer, dc) {
      dc.binaryType = 'arraybuffer';
      if (dc.label === 'fast') peer.dcFast = dc;
      else if (dc.label === 'reliable') peer.dcReliable = dc;
      else if (dc.label === 'meta') peer.dcMeta = dc;

      dc.onopen = () => {
        this.updatePeerTransportState(peer);
        if (dc.label === 'meta') {
          this.sendMetaToPeer(peer.peerId, {
            type: 'identity',
            name: state.localName,
            skinId: state.localSkin,
          });
        }
        renderPager();
        if (state.menuOpen && state.activeTab === 'vice-net') {
          renderViceNetPanel();
        }
      };

      dc.onclose = () => {
        this.updatePeerTransportState(peer);
        renderPager();
      };

      dc.onerror = () => {
        this.updatePeerTransportState(peer);
      };

      dc.onmessage = (ev) => {
        if (dc.label === 'meta') {
          const text = typeof ev.data === 'string' ? ev.data : utf8Decoder.decode(new Uint8Array(ev.data));
          this.handleMetaText(peer.peerId, text);
          return;
        }
        if (ev.data instanceof ArrayBuffer) {
          bridge.mpPushNetPacket(peer.peerId, new Uint8Array(ev.data));
        } else if (ev.data instanceof Blob) {
          ev.data.arrayBuffer().then((buf) => {
            bridge.mpPushNetPacket(peer.peerId, new Uint8Array(buf));
          }).catch(() => {});
        }
      };
    },

    initWebRtcForPeer(peer) {
      if (typeof RTCPeerConnection !== 'function') return;
      if (peer.pc) return;
      if (!state.localPeerId || peer.peerId === state.localPeerId) return;

      try {
        const pc = new RTCPeerConnection(RTC_CONFIG);
        peer.pc = pc;

        pc.onicecandidate = (ev) => {
          if (ev.candidate) {
            this.sendSignal(peer.peerId, {
              type: 'candidate',
              candidate: ev.candidate.toJSON ? ev.candidate.toJSON() : ev.candidate,
            });
          }
        };

        pc.onconnectionstatechange = () => {
          if (pc.connectionState === 'failed' || pc.connectionState === 'closed' || pc.connectionState === 'disconnected') {
            this.updatePeerTransportState(peer);
            renderPager();
          }
        };

        const isOfferer = state.localPeerId < peer.peerId;
        if (isOfferer) {
          const dcFast = pc.createDataChannel('fast', { ordered: false, maxRetransmits: 0 });
          const dcReliable = pc.createDataChannel('reliable', { ordered: true });
          const dcMeta = pc.createDataChannel('meta', { ordered: true });
          this.attachDataChannel(peer, dcFast);
          this.attachDataChannel(peer, dcReliable);
          this.attachDataChannel(peer, dcMeta);

          pc.createOffer()
            .then((offer) => pc.setLocalDescription(offer))
            .then(() => {
              this.sendSignal(peer.peerId, { type: 'offer', sdp: pc.localDescription });
            })
            .catch((err) => {
              console.warn(`[WebRTC] Offer failed for peer ${peer.peerId}:`, err);
            });
        } else {
          pc.ondatachannel = (ev) => {
            this.attachDataChannel(peer, ev.channel);
          };
        }
      } catch (err) {
        console.warn('[WebRTC] PeerConnection init failed, using DO relay fallback:', err);
      }
    },

    async handleSignal(fromPeerId, payloadBytes) {
      let msg = null;
      try {
        msg = JSON.parse(utf8Decoder.decode(payloadBytes));
      } catch (_) {
        return;
      }
      if (!msg || typeof msg !== 'object') return;

      const peer = this.ensurePeer(fromPeerId);
      if (!peer.pc) {
        this.initWebRtcForPeer(peer);
      }
      const pc = peer.pc;
      if (!pc) return;

      try {
        if (msg.type === 'offer' && msg.sdp) {
          await pc.setRemoteDescription(new RTCSessionDescription(msg.sdp));
          while (peer.pendingCandidates.length > 0) {
            const c = peer.pendingCandidates.shift();
            await pc.addIceCandidate(new RTCIceCandidate(c));
          }
          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);
          this.sendSignal(fromPeerId, { type: 'answer', sdp: pc.localDescription });
        } else if (msg.type === 'answer' && msg.sdp) {
          await pc.setRemoteDescription(new RTCSessionDescription(msg.sdp));
          while (peer.pendingCandidates.length > 0) {
            const c = peer.pendingCandidates.shift();
            await pc.addIceCandidate(new RTCIceCandidate(c));
          }
        } else if (msg.type === 'candidate' && msg.candidate) {
          if (pc.remoteDescription && pc.remoteDescription.type) {
            await pc.addIceCandidate(new RTCIceCandidate(msg.candidate));
          } else {
            peer.pendingCandidates.push(msg.candidate);
          }
        }
      } catch (err) {
        console.warn(`[WebRTC] Signal error from peer ${fromPeerId}:`, err);
      }
    },

    sendNetPacket(targetPeer, channel, payloadUint8) {
      const ch = channel === CH_RELIABLE ? CH_RELIABLE : CH_FAST;
      const wsOpen = this.ws && this.ws.readyState === WebSocket.OPEN;

      if (targetPeer <= 0) {
        for (const peer of this.peers.values()) {
          const dc = ch === CH_FAST ? peer.dcFast : peer.dcReliable;
          if (dc && dc.readyState === 'open' && dc.bufferedAmount < 524288) {
            try {
              dc.send(payloadUint8);
              continue;
            } catch (_) {
              // Fallback to DO relay below
            }
          }
          if (wsOpen) {
            this.ws.send(makeFrame(FRAME_DATA, peer.peerId, ch, payloadUint8));
          }
        }
        return;
      }

      const peer = this.peers.get(targetPeer);
      if (peer) {
        const dc = ch === CH_FAST ? peer.dcFast : peer.dcReliable;
        if (dc && dc.readyState === 'open' && dc.bufferedAmount < 524288) {
          try {
            dc.send(payloadUint8);
            return;
          } catch (_) {
            // Fallback to DO relay below
          }
        }
      }
      if (wsOpen) {
        this.ws.send(makeFrame(FRAME_DATA, targetPeer, ch, payloadUint8));
      }
    },

    sendMetaToPeer(targetPeerId, obj) {
      const text = JSON.stringify(obj);
      const peer = this.peers.get(targetPeerId);
      if (peer && peer.dcMeta && peer.dcMeta.readyState === 'open') {
        try {
          peer.dcMeta.send(text);
          return;
        } catch (_) {}
      }
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        this.ws.send(makeFrame(FRAME_DATA, targetPeerId, CH_META, utf8Encoder.encode(text)));
      }
    },

    broadcastMeta(obj) {
      const text = JSON.stringify(obj);
      const bytes = utf8Encoder.encode(text);
      const wsOpen = this.ws && this.ws.readyState === WebSocket.OPEN;
      for (const peer of this.peers.values()) {
        if (peer.dcMeta && peer.dcMeta.readyState === 'open') {
          try {
            peer.dcMeta.send(text);
            continue;
          } catch (_) {}
        }
        if (wsOpen) {
          this.ws.send(makeFrame(FRAME_DATA, peer.peerId, CH_META, bytes));
        }
      }
    },

    handleMetaText(fromPeerId, text) {
      let msg = null;
      try {
        msg = JSON.parse(text);
      } catch (_) {
        return;
      }
      if (!msg || typeof msg !== 'object') return;
      const peer = this.peers.get(fromPeerId);

      if (msg.type === 'ping') {
        this.sendMetaToPeer(fromPeerId, { type: 'pong', t: msg.t });
        return;
      }
      if (msg.type === 'pong') {
        if (peer && typeof msg.t === 'number') {
          peer.rttMs = Math.max(1, Math.round(performance.now() - msg.t));
        }
        return;
      }
      if (msg.type === 'chat') {
        const sender = peer ? peer.name : `Peer ${fromPeerId}`;
        addPagerMessage(`${sender}: ${String(msg.text || '').slice(0, 140)}`, 'chat');
        return;
      }
      if (msg.type === 'identity') {
        const updatedName = sanitizeName(msg.name, peer ? peer.name : `Player ${fromPeerId}`);
        const updatedSkin = clampInt(msg.skinId, 0, 129, 0);
        if (peer) {
          peer.name = updatedName;
          peer.skinId = updatedSkin;
        }
        bridge.mpSetPlayerInfo(fromPeerId, updatedName, updatedSkin);
        if (state.isHost) {
          this.pushRoomInfoSnapshot();
        }
        if (state.menuOpen && state.activeTab === 'vice-net') {
          renderViceNetPanel();
        }
        return;
      }
      if (msg.type === 'world_notice') {
        if (typeof msg.openIslands === 'boolean') {
          state.openIslands = msg.openIslands;
          bridge.mpSetOpenIslands(msg.openIslands);
          syncHostTogglesUI();
        }
        if (msg.text) {
          addPagerMessage(String(msg.text), 'accent');
        }
      }
    },

    pushRoomInfoSnapshot() {
      if (!state.isHost || !this.ws || this.ws.readyState !== WebSocket.OPEN) return;
      const players = [
        { slot: 1, name: state.localName, skinId: clampInt(state.localSkin, 0, 129, 0) },
      ];
      for (const peer of this.peers.values()) {
        if (peer.peerId >= 2 && peer.peerId <= 4) {
          players.push({
            slot: peer.peerId,
            name: peer.name,
            skinId: clampInt(peer.skinId, 0, 129, 0),
          });
        }
      }
      const patch = {
        room_name: state.roomName,
        host_name: state.localName,
        mode: state.coopMode,
        island_unlocked: state.openIslands ? 3 : 1,
        players,
      };
      this.ws.send(makeFrame(FRAME_ROOM_INFO, 0, 0, utf8Encoder.encode(JSON.stringify(patch))));
    },

    kickPeer(peerId) {
      if (!state.isHost || !this.ws || this.ws.readyState !== WebSocket.OPEN) return;
      this.ws.send(makeFrame(FRAME_KICK_PEER, peerId, 0));
    },

    connect(options, forceLocalFallback = false) {
      const mode = options.mode; // 'host' | 'client'
      const port = clampInt(options.roomPort, 1024, 65535, DEFAULT_ROOM_PORT);
      const playerName = sanitizeName(options.name || state.localName, 'Tommy');
      const skinId = clampInt(options.skinId ?? state.localSkin, 0, 129, 0);
      const roomName = sanitizeName(options.roomName || `${playerName}'s Vice City`, `${playerName}'s Vice City`);
      const coopMode = options.coopMode === 'open_sandbox' ? 'open_sandbox' : 'story_coop';

      this.disconnect(true);

      state.mode = mode;
      state.isHost = mode === 'host';
      state.roomPort = port;
      state.roomName = roomName;
      state.coopMode = coopMode;
      state.localName = playerName;

      const params = {
        role: mode,
        room: String(port),
        name: playerName,
        skinId: String(skinId),
      };
      if (mode === 'host') {
        params.room_name = roomName;
        params.mode = coopMode;
      }

      this.connecting = true;
      this.connected = false;
      const wsUrl = this.wsUrlFor(params, forceLocalFallback);
      const ws = new WebSocket(wsUrl);
      ws.binaryType = 'arraybuffer';
      this.ws = ws;
      let wsOpened = false;
      let fallbackTriggered = false;

      const canFallbackLocal = () =>
        !wsOpened &&
        !fallbackTriggered &&
        !forceLocalFallback &&
        !state.localRelayFallback &&
        isLocalHostOrigin() &&
        getRelayHttpBase(false) !== location.origin;

      if (!forceLocalFallback) {
        addPagerMessage(
          mode === 'host'
            ? `VICE NET: Opening room #${port}...`
            : `VICE NET: Connecting to room #${port}...`,
          'accent'
        );
      }

      ws.onopen = () => {
        if (this.ws !== ws) return;
        wsOpened = true;
        this.connecting = false;
        this.connected = true;
        renderPager();
      };

      ws.onmessage = (ev) => {
        if (this.ws !== ws) return;
        if (typeof ev.data === 'string') {
          try {
            const info = JSON.parse(ev.data);
            if (info && typeof info === 'object') {
              this.applyRoomInfo(info);
            }
          } catch (_) {}
          return;
        }
        const buf = new Uint8Array(ev.data);
        if (buf.byteLength < 4) return;
        const frameType = buf[0];
        const headerPeerId = (buf[1] << 8) | buf[2];
        const channel = buf[3];
        const payload = buf.subarray(4);

        this.handleBinaryFrame(frameType, headerPeerId, channel, payload);
      };

      ws.onclose = (ev) => {
        if (this.ws !== ws) return;
        if (canFallbackLocal()) {
          fallbackTriggered = true;
          state.localRelayFallback = true;
          this.ws = null;
          this.connect(options, true);
          return;
        }
        this.ws = null;
        this.connecting = false;
        this.connected = false;
        for (const peer of this.peers.values()) {
          this.cleanupPeer(peer);
        }
        this.peers.clear();
        if (ev.code !== 1000 && !fallbackTriggered) {
          addPagerMessage(`VICE NET: Connection closed (${ev.reason || 'code ' + ev.code}).`, 'warn');
        }
        renderPager();
        renderViceNetPanel();
      };

      ws.onerror = () => {
        if (this.ws !== ws) return;
        if (canFallbackLocal()) {
          fallbackTriggered = true;
          state.localRelayFallback = true;
          this.ws = null;
          try { ws.close(); } catch (_) {}
          this.connect(options, true);
          return;
        }
        addPagerMessage('VICE NET: Relay socket error.', 'warn');
      };
    },

    handleBinaryFrame(frameType, headerPeerId, channel, payload) {
      if (frameType === FRAME_HOST_ASSIGNED) {
        let meta = {};
        try { meta = JSON.parse(utf8Decoder.decode(payload)); } catch (_) {}
        state.localPeerId = meta.peerId || 1;
        state.isHost = true;
        state.roomPort = meta.roomPort || headerPeerId || state.roomPort;

        if (bridge.isReady()) {
          bridge.mpShutdown();
          bridge.mpInit(state.localPeerId, true);
          bridge.mpSetPlayerInfo(state.localPeerId, state.localName, state.localSkin);
          bridge.mpSetOpenIslands(state.openIslands);
        }
        addPagerMessage(`HOST ONLINE: Room #${state.roomPort} (Slot 1/4)`, 'accent');
        this.pushRoomInfoSnapshot();
        renderPager();
        renderViceNetPanel();
        return;
      }

      if (frameType === FRAME_ROOM_INFO) {
        let info = null;
        try { info = JSON.parse(utf8Decoder.decode(payload)); } catch (_) {}
        if (info) this.applyRoomInfo(info);
        return;
      }

      if (frameType === FRAME_PEER_CONNECTED) {
        let meta = {};
        try { meta = JSON.parse(utf8Decoder.decode(payload)); } catch (_) {}
        const remoteId = meta.peerId || headerPeerId;
        const myAssigned = meta.yourPeerId || (channel > 0 ? channel : 0);

        if (myAssigned > 0 && state.localPeerId !== myAssigned && !state.isHost) {
          state.localPeerId = myAssigned;
          if (bridge.isReady()) {
            bridge.mpShutdown();
            bridge.mpInit(state.localPeerId, false);
            bridge.mpSetPlayerInfo(state.localPeerId, state.localName, state.localSkin);
          }
          addPagerMessage(`JOINED ROOM #${state.roomPort} as Slot ${state.localPeerId}`, 'accent');
        }

        if (remoteId && remoteId !== state.localPeerId) {
          const peer = this.ensurePeer(remoteId, meta.name, meta.skinId ?? 0);
          bridge.mpSetPlayerInfo(peer.peerId, peer.name, peer.skinId);
          addPagerMessage(`${peer.name} (Slot ${peer.peerId}) connected.`, 'normal');
          this.initWebRtcForPeer(peer);
          this.sendMetaToPeer(peer.peerId, {
            type: 'identity',
            name: state.localName,
            skinId: state.localSkin,
          });
          if (state.isHost && state.openIslands) {
            this.sendMetaToPeer(peer.peerId, {
              type: 'world_notice',
              openIslands: true,
              text: 'Host enabled Open All Bridges & Mainland.',
            });
          }
        }
        renderPager();
        renderViceNetPanel();
        return;
      }

      if (frameType === FRAME_PEER_DISCONNECTED) {
        const peer = this.peers.get(headerPeerId);
        if (peer) {
          addPagerMessage(`${peer.name} (Slot ${headerPeerId}) disconnected.`, 'warn');
          this.cleanupPeer(peer);
          this.peers.delete(headerPeerId);
        }
        if (headerPeerId === 1 && !state.isHost) {
          addPagerMessage('Host ended the session.', 'warn');
        }
        if (wasm.mod && typeof wasm.mod._mp_on_peer_disconnected === 'function') {
          wasm.mod._mp_on_peer_disconnected(headerPeerId | 0);
        }
        renderPager();
        renderViceNetPanel();
        return;
      }

      if (frameType === FRAME_REJECTED) {
        const reason = utf8Decoder.decode(payload) || 'Connection rejected';
        addPagerMessage(`REJECTED: ${reason}`, 'warn');
        this.disconnect(true);
        state.mode = 'offline';
        state.isHost = true;
        state.localPeerId = 1;
        if (bridge.isReady()) {
          bridge.mpShutdown();
          bridge.mpInit(1, true);
          bridge.mpSetPlayerInfo(1, state.localName, state.localSkin);
        }
        renderPager();
        renderViceNetPanel();
        return;
      }

      if (frameType === FRAME_WEBRTC_SIGNAL) {
        this.handleSignal(headerPeerId, payload);
        return;
      }

      if (frameType === FRAME_DATA) {
        if (channel === CH_META) {
          this.handleMetaText(headerPeerId, utf8Decoder.decode(payload));
          return;
        }
        bridge.mpPushNetPacket(headerPeerId, payload);
      }
    },

    applyRoomInfo(info) {
      state.roomInfo = info;
      if (info.yourPeerId && !state.isHost) {
        state.localPeerId = info.yourPeerId;
      }
      if (info.mode && !state.isHost) {
        state.coopMode = info.mode === 'open_sandbox' ? 'open_sandbox' : 'story_coop';
        if (state.coopMode === 'open_sandbox') {
          state.openIslands = true;
          bridge.mpSetOpenIslands(2);
          syncHostTogglesUI();
        }
      }
      if (typeof info.island_unlocked === 'number' && !state.isHost) {
        const shouldOpen = state.coopMode === 'open_sandbox' || info.island_unlocked >= 3;
        if (state.openIslands !== shouldOpen || state.coopMode === 'open_sandbox') {
          state.openIslands = shouldOpen;
          bridge.mpSetOpenIslands(state.coopMode === 'open_sandbox' ? 2 : (shouldOpen ? 1 : 0));
          syncHostTogglesUI();
        }
      }
      if (Array.isArray(info.players)) {
        for (const p of info.players) {
          if (!p || p.slot === state.localPeerId) continue;
          const peer = this.peers.get(p.slot);
          if (peer) {
            if (p.name) peer.name = sanitizeName(p.name, peer.name);
            if (typeof p.skinId === 'number') peer.skinId = clampInt(p.skinId, 0, 129, peer.skinId);
            bridge.mpSetPlayerInfo(peer.peerId, peer.name, peer.skinId);
          }
        }
      }
      renderPager();
      if (state.menuOpen && state.activeTab === 'vice-net') {
        renderViceNetPanel();
      }
    },
  };

  // ============================================================================
  // HTTP API Client (Rooms, Auth, SQLite Cloud Campaigns)
  // ============================================================================
  async function apiFetch(path, options = {}) {
    const headers = new Headers(options.headers || {});
    if (options.body && !headers.has('Content-Type')) {
      headers.set('Content-Type', 'application/json');
    }
    if (state.authToken) {
      headers.set('Authorization', `Bearer ${state.authToken}`);
    }
    const isAbsolute = /^https?:\/\//i.test(path);
    const primaryUrl = isAbsolute ? path : `${getRelayHttpBase()}${path}`;
    let res = null;
    try {
      res = await fetch(primaryUrl, { ...options, headers });
      if (
        !isAbsolute &&
        !state.localRelayFallback &&
        isLocalHostOrigin() &&
        getRelayHttpBase() !== location.origin &&
        (res.status >= 500 || res.status === 404)
      ) {
        const localRes = await fetch(`${location.origin}${path}`, { ...options, headers });
        state.localRelayFallback = true;
        res = localRes;
      }
    } catch (fetchErr) {
      if (
        !isAbsolute &&
        !state.localRelayFallback &&
        isLocalHostOrigin() &&
        getRelayHttpBase() !== location.origin
      ) {
        res = await fetch(`${location.origin}${path}`, { ...options, headers });
        state.localRelayFallback = true;
      } else {
        throw fetchErr;
      }
    }
    let data = null;
    try {
      data = await res.json();
    } catch (_) {
      data = { ok: false, error: `HTTP ${res.status}` };
    }
    if (!res.ok) {
      const msg = (data && data.error) || `Request failed (HTTP ${res.status})`;
      const err = new Error(msg);
      err.status = res.status;
      throw err;
    }
    return data;
  }

  async function refreshRooms() {
    try {
      const data = await apiFetch('/api/rooms', { method: 'GET' });
      state.rooms = Array.isArray(data.rooms) ? data.rooms : [];
    } catch (_) {
      state.rooms = [];
    }
    renderSplashRooms();
    renderViceNetRooms();
  }

  async function refreshAuthSession() {
    if (!state.authToken) {
      state.authUser = null;
      state.campaigns = [];
      renderSavesPanel();
      return;
    }
    try {
      const data = await apiFetch('/api/auth/me', { method: 'GET' });
      state.authUser = data.user || null;
      await refreshCampaigns();
    } catch (_) {
      state.authToken = '';
      state.authUser = null;
      state.campaigns = [];
      try { localStorage.removeItem(LS_TOKEN_KEY); } catch (_) {}
      renderSavesPanel();
    }
  }

  async function refreshCampaigns() {
    if (!state.authToken) {
      state.campaigns = [];
      renderSavesPanel();
      return;
    }
    try {
      const data = await apiFetch('/api/campaigns', { method: 'GET' });
      state.campaigns = Array.isArray(data.campaigns) ? data.campaigns : [];
    } catch (err) {
      setSavesStatus(err.message, true);
    }
    renderSavesPanel();
  }

  async function handleAuthSubmit(isRegister) {
    const userEl = document.getElementById('auth-username');
    const passEl = document.getElementById('auth-password');
    const username = userEl ? userEl.value.trim() : '';
    const password = passEl ? passEl.value : '';
    if (!username || !password) {
      setSavesStatus('Enter both username and password.', true);
      return;
    }
    try {
      const endpoint = isRegister ? '/api/auth/register' : '/api/auth/login';
      const res = await apiFetch(endpoint, {
        method: 'POST',
        body: JSON.stringify({ username, password }),
      });
      state.authToken = res.token || '';
      state.authUser = res.user || null;
      try { localStorage.setItem(LS_TOKEN_KEY, state.authToken); } catch (_) {}
      if (passEl) passEl.value = '';
      setSavesStatus(`Signed in as ${state.authUser?.username || username}.`, false);
      await refreshCampaigns();
    } catch (err) {
      setSavesStatus(err.message, true);
    }
  }

  async function handleLogout() {
    try {
      if (state.authToken) {
        await apiFetch('/api/auth/logout', { method: 'POST' });
      }
    } catch (_) {}
    state.authToken = '';
    state.authUser = null;
    state.campaigns = [];
    try { localStorage.removeItem(LS_TOKEN_KEY); } catch (_) {}
    setSavesStatus('Signed out.', false);
    renderSavesPanel();
  }

  function buildSlotsForSave() {
    const hud = state.hud || {};
    const localState = bridge.mpGetLocalPlayerState();
    const localFloats = localState ? localState.floats : [];
    const localInts = localState ? localState.ints : [];
    const hudPeers = Array.isArray(hud.peers || hud.players) ? (hud.peers || hud.players) : [];

    function findHudPeer(slotNum) {
      return hudPeers.find((p) => Number(p.peerId ?? p.id ?? p.slot) === slotNum) || null;
    }

    const slots = [];
    for (let slotIdx = 0; slotIdx < 4; slotIdx++) {
      const peerId = slotIdx + 1;
      const hpHud = findHudPeer(peerId);
      if (peerId === state.localPeerId) {
        slots.push({
          slot_idx: slotIdx,
          user_id: state.authUser ? state.authUser.userId : null,
          character_name: state.localName,
          skin_id: clampInt(state.localSkin, 0, 129, 0),
          money: clampInt(hud.money ?? hpHud?.money ?? localInts[0] ?? 0, -99999999, 999999999, 0),
          health: Number(hud.hp ?? hud.health ?? hpHud?.hp ?? localFloats[4] ?? 100),
          armor: Number(hud.armor ?? hud.armour ?? hpHud?.armor ?? localFloats[5] ?? 0),
          weapons: Array.isArray(hud.weapons) ? hud.weapons : [],
          pos: {
            x: Number(hud.x ?? hpHud?.x ?? localFloats[0] ?? 0),
            y: Number(hud.y ?? hpHud?.y ?? localFloats[1] ?? 0),
            z: Number(hud.z ?? hpHud?.z ?? localFloats[2] ?? 0),
            district: hud.district || hud.zone || '',
          },
        });
      } else {
        const peer = net.peers.get(peerId);
        slots.push({
          slot_idx: slotIdx,
          user_id: null,
          character_name: peer ? peer.name : (hpHud?.name || `Player ${peerId}`),
          skin_id: clampInt(peer ? peer.skinId : (hpHud?.skinId ?? 0), 0, 129, 0),
          money: clampInt(hpHud?.money ?? 0, -99999999, 999999999, 0),
          health: Number(hpHud?.hp ?? hpHud?.health ?? 100),
          armor: Number(hpHud?.armor ?? hpHud?.armour ?? 0),
          weapons: Array.isArray(hpHud?.weapons) ? hpHud.weapons : [],
          pos: {
            x: Number(hpHud?.x ?? 0),
            y: Number(hpHud?.y ?? 0),
            z: Number(hpHud?.z ?? 0),
            district: hpHud?.district || hpHud?.zone || '',
          },
        });
      }
    }
    return slots;
  }

  async function saveCurrentCampaign(overwriteCampaignId = '') {
    if (!state.authUser) {
      setSavesStatus('Sign in to save to Cloudflare SQLite.', true);
      return;
    }
    const titleEl = document.getElementById('save-campaign-title');
    const title = sanitizeName(titleEl ? titleEl.value : '', `${state.localName}'s Vice City`);

    const rawBytes = bridge.mpExportSaveState();
    if (!rawBytes || rawBytes.byteLength === 0) {
      setSavesStatus('Engine save state unavailable yet (wait until Vice City loads).', true);
      return;
    }

    setSavesStatus('Compressing & uploading safehouse save...', false);
    try {
      const saveBlobGzB64 = await compressBytesToGzipB64(rawBytes);
      const payload = {
        campaign_id: overwriteCampaignId || undefined,
        title,
        mode: state.coopMode,
        world_state: {
          island_unlocked: state.openIslands ? 3 : 1,
          district: state.hud?.district || state.hud?.zone || 'Ocean Beach',
          savedAt: Date.now(),
        },
        save_blob_gz_b64: saveBlobGzB64,
        slots: buildSlotsForSave(),
      };
      const res = await apiFetch('/api/campaigns/save', {
        method: 'POST',
        body: JSON.stringify(payload),
      });
      setSavesStatus(
        `Saved "${title}" (Rev ${res.save_rev}, ${formatBytes(rawBytes.byteLength)} raw).`,
        false
      );
      addPagerMessage(`SAFEHOUSE: Saved "${title}" (Rev ${res.save_rev}).`, 'accent');
      await refreshCampaigns();
    } catch (err) {
      setSavesStatus(err.message, true);
    }
  }

  async function loadCloudCampaign(campaignId) {
    if (!state.authUser) {
      setSavesStatus('Sign in to load cloud saves.', true);
      return;
    }
    if (state.mode === 'client' && !state.isHost) {
      setSavesStatus('Only the room Host can load a world save into a co-op room.', true);
      return;
    }
    setSavesStatus('Downloading & restoring safehouse save...', false);
    try {
      const data = await apiFetch(`/api/campaigns/${encodeURIComponent(campaignId)}/load`, {
        method: 'GET',
      });
      if (!data.save_blob_gz_b64) {
        throw new Error('Save blob is empty');
      }
      const bytes = await decompressGzipB64ToBytes(data.save_blob_gz_b64);
      const ok = bridge.mpImportSaveState(bytes);
      if (!ok) {
        throw new Error('Engine rejected save state import');
      }

      const openIslands = Boolean(data.world_state && Number(data.world_state.island_unlocked) >= 3);
      state.openIslands = openIslands;
      bridge.mpSetOpenIslands(openIslands);
      syncHostTogglesUI();

      if (state.isHost && net.connected) {
        net.pushRoomInfoSnapshot();
        net.broadcastMeta({
          type: 'world_notice',
          openIslands,
          text: `Host loaded safehouse save: "${data.title}" (Rev ${data.save_rev}).`,
        });
      }
      setSavesStatus(`Loaded "${data.title}" (Rev ${data.save_rev}).`, false);
      addPagerMessage(`SAFEHOUSE: Loaded "${data.title}" (Rev ${data.save_rev}).`, 'accent');
    } catch (err) {
      setSavesStatus(err.message, true);
    }
  }

  async function deleteCloudCampaign(campaignId) {
    if (!state.authUser) return;
    try {
      await apiFetch(`/api/campaigns/${encodeURIComponent(campaignId)}`, { method: 'DELETE' });
      setSavesStatus('Campaign deleted.', false);
      await refreshCampaigns();
    } catch (err) {
      setSavesStatus(err.message, true);
    }
  }

  function exportLocalSaveFile() {
    const rawBytes = bridge.mpExportSaveState();
    if (!rawBytes || rawBytes.byteLength === 0) {
      setSavesStatus('Engine save state unavailable yet (wait until world is loaded).', true);
      return;
    }
    const blob = new Blob([rawBytes], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'GTAVCsf1.b';
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
    setSavesStatus(`Downloaded GTAVCsf1.b (${formatBytes(rawBytes.byteLength)}).`, false);
    addPagerMessage('SAFEHOUSE: Exported local GTAVCsf1.b save.', 'accent');
  }

  async function importLocalSaveFile(file) {
    if (!file) return;
    if (state.mode === 'client' && !state.isHost) {
      setSavesStatus('Only the room Host can import a save state into a co-op room.', true);
      return;
    }
    try {
      const buf = await file.arrayBuffer();
      const bytes = new Uint8Array(buf);
      if (bytes.byteLength === 0) {
        throw new Error('Selected .b file is empty');
      }
      const ok = bridge.mpImportSaveState(bytes);
      if (!ok) {
        throw new Error('Engine rejected .b save import');
      }
      setSavesStatus(`Imported ${file.name} (${formatBytes(bytes.byteLength)}).`, false);
      addPagerMessage(`SAFEHOUSE: Imported local save "${file.name}".`, 'accent');
      if (state.isHost && net.connected) {
        net.broadcastMeta({
          type: 'world_notice',
          text: `Host imported local safehouse save "${file.name}".`,
        });
      }
    } catch (err) {
      setSavesStatus(err.message, true);
    }
  }

  // ============================================================================
  // WASM Boot Sequence (/revc.js + window.createGtaVcModule)
  // ============================================================================
  function loadScriptOnce(src) {
    return new Promise((resolve, reject) => {
      if (typeof window.createGtaVcModule === 'function') {
        resolve();
        return;
      }
      const existing = document.querySelector(`script[data-gtavc-src="${src}"]`);
      if (existing) {
        existing.addEventListener('load', () => resolve(), { once: true });
        existing.addEventListener('error', () => reject(new Error(`Failed to load ${src}`)), { once: true });
        return;
      }
      const script = document.createElement('script');
      script.src = src;
      script.async = true;
      script.setAttribute('data-gtavc-src', src);
      script.onload = () => resolve();
      script.onerror = () => reject(new Error(`Engine script ${src} not found (build revc.js + revc.wasm first)`));
      document.head.appendChild(script);
    });
  }

  function updateBootProgressUI(info) {
    const fillEl = document.getElementById('boot-progress-fill');
    const textEl = document.getElementById('boot-progress-text');
    const metaEl = document.getElementById('boot-progress-meta');
    if (!fillEl || !textEl || !metaEl) return;

    const total = info.totalBytes || 0;
    const loaded = info.loadedBytes || 0;
    const pct = total > 0 ? Math.min(100, Math.round((loaded / total) * 100)) : 0;
    fillEl.style.width = `${pct}%`;

    const label = info.pack === 'cuts' ? 'Cutscene Pack' : 'Core Assets';
    const cacheTag = info.cacheHit ? ' [IDB CACHE]' : '';
    const phaseMap = {
      manifest: 'Fetching asset manifest...',
      'cache-check': `Checking browser VFS cache (${label})...`,
      'cache-hit': `Instant cache hit (${label})`,
      download: `Downloading ${label}${cacheTag}...`,
      verify: `Verifying SHA-256 checksum (${label})...`,
      decompress: `Decompressing VFS Archive (${label})...`,
      unpack: `Mounting MEMFS: ${info.currentFile || `${info.fileIndex || 0}/${info.fileCount || '?'}`}`,
      ready: `${label} mounted (${info.fileCount || 0} files)${cacheTag}`,
    };
    textEl.textContent = phaseMap[info.phase] || `${info.phase}...`;
    metaEl.textContent = total > 0
      ? `${pct}% • ${formatBytes(loaded)} / ${formatBytes(total)}`
      : 'Preparing...';
  }

  function setBootError(msg) {
    const errEl = document.getElementById('boot-error');
    if (!errEl) return;
    if (!msg) {
      errEl.hidden = true;
      errEl.textContent = '';
    } else {
      errEl.hidden = false;
      errEl.textContent = msg;
    }
  }

  async function mountCutscenePackInBackground() {
    if (!window.GtaAssetLoader || !wasm.mod || !wasm.mod.FS) return false;
    await window.GtaAssetLoader.loadCuts(wasm.mod.FS, (info) => {
      if (info.phase === 'ready') {
        addPagerMessage('CUTSCENE PACK: anim/cuts.img mounted!', 'accent');
      }
    });
    refreshSafehouseCutsBanner();
    refreshCacheStatusUI();
    return true;
  }

  async function launchGame() {
    if (state.booting || state.booted) return;
    setBootError('');

    const nameInput = document.getElementById('boot-player-name');
    const skinSelect = document.getElementById('boot-skin-select');
    const roomNameInput = document.getElementById('boot-room-name');
    const roomPortInput = document.getElementById('boot-room-port');
    const coopModeSelect = document.getElementById('boot-coop-mode');
    const openIslandsCb = document.getElementById('boot-open-islands');
    const cutsCb = document.getElementById('boot-load-cuts');

    state.localName = sanitizeName(nameInput ? nameInput.value : state.localName, 'Tommy');
    state.localSkin = clampInt(skinSelect ? skinSelect.value : state.localSkin, 0, 129, 0);
    state.roomName = sanitizeName(roomNameInput ? roomNameInput.value : `${state.localName}'s Vice City`, `${state.localName}'s Vice City`);
    state.roomPort = clampInt(roomPortInput ? roomPortInput.value : DEFAULT_ROOM_PORT, 1024, 65535, DEFAULT_ROOM_PORT);
    state.coopMode = coopModeSelect && coopModeSelect.value === 'open_sandbox' ? 'open_sandbox' : 'story_coop';
    state.openIslands = Boolean(openIslandsCb && openIslandsCb.checked) || state.coopMode === 'open_sandbox';
    state.loadCutsOnBoot = Boolean(cutsCb && cutsCb.checked) && state.coopMode !== 'open_sandbox';

    if (state.mode === 'client') {
      try {
        if (!Array.isArray(state.rooms) || state.rooms.length === 0) await refreshRooms();
      } catch (_) {}
      const targetRoom = Array.isArray(state.rooms)
        ? state.rooms.find((r) => Number(r.game_port) === Number(state.roomPort))
        : null;
      if (targetRoom && targetRoom.mode === 'open_sandbox') {
        state.coopMode = 'open_sandbox';
        state.openIslands = true;
        state.loadCutsOnBoot = false;
      }
    }

    try {
      localStorage.setItem(LS_NAME_KEY, state.localName);
      localStorage.setItem(LS_SKIN_KEY, String(state.localSkin));
    } catch (_) {}

    if (!window.GtaAssetLoader || !window.GtaAudioStream) {
      setBootError('asset_loader.js failed to initialize.');
      return;
    }

    window.GtaAudioStream.unlock();

    state.booting = true;
    const launchBtn = document.getElementById('boot-launch-btn');
    const progressWrap = document.getElementById('boot-progress-wrap');
    if (launchBtn) launchBtn.disabled = true;
    if (progressWrap) progressWrap.hidden = false;

    try {
      updateBootProgressUI({ phase: 'manifest', loadedBytes: 0, totalBytes: 0, pack: 'core' });
      const textEl = document.getElementById('boot-progress-text');
      if (textEl) textEl.textContent = 'Loading reVC WebAssembly runtime...';

      const buildBust = Date.now();
      await loadScriptOnce(`/revc.js?v=${buildBust}`);
      if (typeof window.createGtaVcModule !== 'function') {
        throw new Error('window.createGtaVcModule is missing in /revc.js');
      }

      const canvas = document.getElementById('canvas');
      resizeCanvas();

      const moduleConfig = {
        canvas,
        arguments: state.coopMode === 'open_sandbox' ? ['--freeroam'] : [],
        locateFile(path) {
          return '/' + String(path || '').replace(/^\/+/, '') + `?v=${buildBust}`;
        },
        print(txt) {
          if (txt) {
            console.log('[reVC]', txt);
          }
        },
        printErr(txt) {
          if (txt) {
            console.warn('[reVC:err]', txt);
          }
        },
        preRun: [
          (mod) => {
            moduleConfig._instance = mod;
            mod.addRunDependency('gtavc-vfs');
            (async () => {
              try {
                await window.GtaAssetLoader.loadCore(mod.FS, updateBootProgressUI);
                if (state.loadCutsOnBoot) {
                  await window.GtaAssetLoader.loadCuts(mod.FS, updateBootProgressUI);
                }
                mod.removeRunDependency('gtavc-vfs');
              } catch (err) {
                setBootError(err && err.message ? err.message : String(err));
              }
            })();
          },
        ],
        onRuntimeInitialized() {
          const m = this && this.HEAPU8 ? this : (moduleConfig._instance || null);
          onWasmRuntimeReady(m);
        },
      };

      const instance = await window.createGtaVcModule(moduleConfig);
      resizeCanvas();
      moduleConfig._instance = instance;
      onWasmRuntimeReady(instance);

      state.booted = true;
      state.booting = false;

      const splash = document.getElementById('splash-gate');
      const hudLayer = document.getElementById('hud-layer');
      if (splash) splash.hidden = true;
      if (hudLayer) hudLayer.hidden = false;

      if (state.mode === 'host' || state.mode === 'client') {
        net.connect({
          mode: state.mode,
          roomPort: state.roomPort,
          roomName: state.roomName,
          coopMode: state.coopMode,
          name: state.localName,
          skinId: state.localSkin,
        });
      } else {
        state.localPeerId = 1;
        state.isHost = true;
        if (!wasm.offlineInited) {
          wasm.offlineInited = true;
          bridge.mpInit(1, true);
          bridge.mpSetPlayerInfo(1, state.localName, state.localSkin);
          bridge.mpSetOpenIslands(state.coopMode === 'open_sandbox' ? 2 : state.openIslands);
        }
        addPagerMessage(
          state.coopMode === 'open_sandbox'
            ? 'FREE ROAM: All bridges unlocked & story cutscenes disabled.'
            : 'OFFLINE SESSION: Welcome to Vice City (1986).',
          'accent'
        );
      }

      if (state.coopMode === 'story_coop' && !state.loadCutsOnBoot && window.GtaAssetLoader && !window.GtaAssetLoader.isPackMounted('cuts')) {
        addPagerMessage('STORY TIP: Load Cutscene Pack (~85 MiB) in [TAB] Tab 3/5 if desired.', 'normal');
      }

      startRuntimeLoops();
      syncAllPanelsFromState();
      renderPager();
      wakePager();
    } catch (err) {
      state.booting = false;
      if (launchBtn) launchBtn.disabled = false;
      setBootError(err && err.message ? err.message : String(err));
    }
  }

  function onWasmRuntimeReady(mod) {
    if (!mod || wasm.runtimeReady) return;
    wasm.mod = mod;
    wasm.runtimeReady = true;
    ensureScratchBuffers(mod);

    if (state.mode === 'offline' && !wasm.offlineInited) {
      wasm.offlineInited = true;
      bridge.mpInit(1, true);
      bridge.mpSetPlayerInfo(1, state.localName, state.localSkin);
      bridge.mpSetOpenIslands(state.coopMode === 'open_sandbox' ? 2 : state.openIslands);
    } else if (state.mode === 'client' && !wasm.clientPreInited) {
      wasm.clientPreInited = true;
      bridge.mpInit(2, false);
      bridge.mpSetPlayerInfo(2, state.localName, state.localSkin);
      bridge.mpSetOpenIslands(state.coopMode === 'open_sandbox' ? 2 : state.openIslands);
    }

    syncFrontendSettingsFromEngine();
  }

  // ============================================================================
  // Frame Loops (60Hz Net Pump + 10Hz HUD Poll + 2s RTT Ping)
  // ============================================================================
  let loopsStarted = false;
  function startRuntimeLoops() {
    if (loopsStarted) return;
    loopsStarted = true;

    function netFrameLoop() {
      if (state.booted && bridge.isReady()) {
        bridge.drainNetPackets((targetPeer, channel, payload) => {
          if (net.connected) {
            net.sendNetPacket(targetPeer, channel, payload);
          }
        });
      }
      requestAnimationFrame(netFrameLoop);
    }
    requestAnimationFrame(netFrameLoop);

    setInterval(() => {
      if (!state.booted || !bridge.isReady()) return;
      const hud = bridge.mpGetHudState();
      if (hud) {
        state.hud = hud;
        if (!wasm.engineLiveSyncDone) {
          wasm.engineLiveSyncDone = true;
          bridge.mpSetPlayerInfo(state.localPeerId, state.localName, state.localSkin);
          bridge.mpSetOpenIslands(state.coopMode === 'open_sandbox' ? 2 : state.openIslands);
          syncFrontendSettingsFromEngine();
        }
        renderPager();
        if (state.menuOpen && state.activeTab === 'vice-net') {
          renderViceNetRoster();
        }
      }
    }, 100);

    setInterval(() => {
      if (!net.connected || net.peers.size === 0) return;
      const now = performance.now();
      for (const peer of net.peers.values()) {
        peer.pingSentAt = now;
        net.sendMetaToPeer(peer.peerId, { type: 'ping', t: now });
      }
    }, 2000);

    setInterval(() => {
      if (!state.booted || (state.menuOpen && state.activeTab === 'vice-net')) {
        refreshRooms();
      }
    }, 5000);
  }

  // ============================================================================
  // Pointer Lock & Pause Overlay Management (Never pauses MP simulation!)
  // ============================================================================
  function requestCanvasPointerLock() {
    if (!state.booted || state.menuOpen) return;
    const canvas = document.getElementById('canvas');
    if (!canvas || document.pointerLockElement === canvas) return;
    try {
      const res = canvas.requestPointerLock && canvas.requestPointerLock();
      if (res && typeof res.catch === 'function') {
        res.catch(() => {});
      }
    } catch (_) {}
  }

  function setMenuOpen(open, tabName = null) {
    if (!state.booted) return;
    state.menuOpen = Boolean(open);
    if (tabName) state.activeTab = tabName;

    const overlay = document.getElementById('pause-overlay');
    if (overlay) overlay.hidden = !state.menuOpen;

    bridge.mpSetUiPauseBlock(state.menuOpen);

    if (state.menuOpen) {
      if (document.pointerLockElement) {
        try { document.exitPointerLock(); } catch (_) {}
      }
      syncAllPanelsFromState();
      if (state.activeTab === 'vice-net') refreshRooms();
      if (state.activeTab === 'safehouse') {
        refreshAuthSession();
        refreshSafehouseCutsBanner();
      }
      if (state.activeTab === 'options') {
        syncFrontendSettingsFromEngine();
        refreshCacheStatusUI();
      }
    } else {
      if (document.activeElement && isEditableTarget(document.activeElement)) {
        document.activeElement.blur();
      }
      requestCanvasPointerLock();
    }
    updatePointerHint();
    wakePager();
  }

  function switchPauseTab(tabName) {
    state.activeTab = tabName;
    document.querySelectorAll('.vc-tab-btn').forEach((btn) => {
      btn.classList.toggle('active', btn.dataset.tab === tabName);
    });
    document.querySelectorAll('.vc-tab-panel').forEach((panel) => {
      panel.hidden = panel.dataset.tabPanel !== tabName;
    });
    if (tabName === 'vice-net') {
      refreshRooms();
      renderViceNetPanel();
    } else if (tabName === 'identity') {
      renderIdentityPreview();
    } else if (tabName === 'safehouse') {
      refreshAuthSession();
      refreshSafehouseCutsBanner();
    } else if (tabName === 'cheats') {
      renderCheatsAuthorityBanner();
    } else if (tabName === 'options') {
      syncFrontendSettingsFromEngine();
      refreshCacheStatusUI();
    }
  }

  function updatePointerHint() {
    const hint = document.getElementById('pointer-hint');
    if (!hint) return;
    hint.hidden = !state.booted || state.menuOpen || state.pointerLocked;
  }

  function resizeCanvas() {
    const canvas = document.getElementById('canvas');
    if (!canvas) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const cssW = window.innerWidth || document.documentElement.clientWidth || 1280;
    const cssH = window.innerHeight || document.documentElement.clientHeight || 720;
    const w = Math.max(320, Math.floor(cssW * dpr));
    const h = Math.max(240, Math.floor(cssH * dpr));
    canvas.style.removeProperty('width');
    canvas.style.removeProperty('height');
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
  }

  // ============================================================================
  // UI Renderers
  // ============================================================================
  function renderPager() {
    const statusEl = document.getElementById('pager-room-status');
    const peersEl = document.getElementById('pager-peers');
    const logEl = document.getElementById('pager-log');
    const bodyEl = document.getElementById('pager-body');
    const minBtn = document.getElementById('pager-minimize-btn');

    if (bodyEl) bodyEl.hidden = state.pagerMinimized;
    if (minBtn) minBtn.textContent = state.pagerMinimized ? '[+]' : '[-]';

    if (statusEl) {
      if (state.mode === 'offline') {
        statusEl.textContent = `OFFLINE • ${state.localName.toUpperCase()}`;
      } else {
        const count = 1 + net.peers.size;
        const roleTag = state.isHost ? 'HOST' : `SLOT ${state.localPeerId}`;
        const connTag = net.connected ? `#${state.roomPort}` : 'CONNECTING';
        statusEl.textContent = `ROOM ${connTag} • ${roleTag} • ${count}/4`;
      }
    }

    if (peersEl) {
      if (state.mode === 'offline') {
        const hp = state.hud?.hp ?? state.hud?.health ?? 100;
        const district = state.hud?.district || state.hud?.zone || 'Vice City';
        peersEl.innerHTML = `<div class="pager-peer-row"><span>[1] ${escapeHtml(state.localName)}</span><span>HP ${Math.round(hp)} • ${escapeHtml(district)}</span></div>`;
      } else {
        const hudPeers = Array.isArray(state.hud?.peers || state.hud?.players)
          ? (state.hud.peers || state.hud.players)
          : [];
        const rows = [];
        const localHp = state.hud?.hp ?? state.hud?.health ?? 100;
        const localDist = state.hud?.district || state.hud?.zone || 'VC';
        rows.push(
          `<div class="pager-peer-row self"><span>[${state.localPeerId}] ${escapeHtml(state.localName)}</span><span>HP ${Math.round(localHp)} • ${escapeHtml(localDist)}</span></div>`
        );
        const sortedPeers = Array.from(net.peers.values()).sort((a, b) => a.peerId - b.peerId);
        for (const p of sortedPeers) {
          const hpObj = hudPeers.find((x) => Number(x.peerId ?? x.id ?? x.slot) === p.peerId);
          const hp = hpObj ? Math.round(hpObj.hp ?? hpObj.health ?? 100) : '--';
          const pingStr = p.rttMs != null ? `${p.rttMs}ms` : '--';
          const trClass = p.transport === 'P2P' ? 'badge-p2p' : 'badge-relay';
          rows.push(
            `<div class="pager-peer-row"><span>[${p.peerId}] ${escapeHtml(p.name)}</span><span>HP ${hp} <em class="${trClass}">${p.transport}</em> ${pingStr}</span></div>`
          );
        }
        peersEl.innerHTML = rows.join('');
      }
    }

    if (logEl) {
      const recent = state.pagerMessages.slice(-4);
      if (recent.length === 0) {
        logEl.innerHTML = '<div class="pager-msg muted">NO NEW MESSAGES...</div>';
      } else {
        logEl.innerHTML = recent
          .map((m) => `<div class="pager-msg ${m.tone}">[${m.time}] ${escapeHtml(m.text)}</div>`)
          .join('');
      }
    }
  }

  function renderSplashRooms() {
    const listEl = document.getElementById('boot-rooms-list');
    if (!listEl) return;
    if (state.rooms.length === 0) {
      listEl.innerHTML = '<div class="empty-note">No active Vice Net rooms found right now.</div>';
      return;
    }
    listEl.innerHTML = state.rooms
      .map(
        (r) => `
      <div class="room-row">
        <div>
          <strong>${escapeHtml(r.room_name)}</strong>
          <div class="room-sub">Host: ${escapeHtml(r.host_name)} • Port #${r.game_port} • ${r.player_count}/${r.member_limit} • ${r.mode === 'open_sandbox' ? 'Free Roam' : 'Story'}</div>
        </div>
        <button type="button" class="vc-btn small" data-boot-join-port="${r.game_port}" ${r.joinable ? '' : 'disabled'}>
          ${r.joinable ? 'SELECT' : 'FULL'}
        </button>
      </div>`
      )
      .join('');

    listEl.querySelectorAll('[data-boot-join-port]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const portInput = document.getElementById('boot-room-port');
        if (portInput) portInput.value = btn.getAttribute('data-boot-join-port');
      });
    });
  }

  function renderViceNetRooms() {
    const listEl = document.getElementById('net-rooms-list');
    if (!listEl) return;
    if (state.rooms.length === 0) {
      listEl.innerHTML = '<div class="empty-note">No public co-op rooms online. Host one above!</div>';
      return;
    }
    listEl.innerHTML = state.rooms
      .map(
        (r) => `
      <div class="room-row">
        <div>
          <strong>${escapeHtml(r.room_name)}</strong>
          <div class="room-sub">Host: ${escapeHtml(r.host_name)} • Port #${r.game_port} • ${r.player_count}/${r.member_limit} • ${r.mode === 'open_sandbox' ? 'Free Roam' : 'Story'}</div>
        </div>
        <button type="button" class="vc-btn small" data-net-join-port="${r.game_port}" ${r.joinable ? '' : 'disabled'}>
          ${r.joinable ? 'JOIN ROOM' : 'FULL'}
        </button>
      </div>`
      )
      .join('');

    listEl.querySelectorAll('[data-net-join-port]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const port = clampInt(btn.getAttribute('data-net-join-port'), 1024, 65535, DEFAULT_ROOM_PORT);
        const portInput = document.getElementById('net-room-port');
        if (portInput) portInput.value = String(port);
        net.connect({
          mode: 'client',
          roomPort: port,
          name: state.localName,
          skinId: state.localSkin,
        });
      });
    });
  }

  function renderViceNetRoster() {
    const rosterEl = document.getElementById('net-peer-roster');
    if (!rosterEl) return;

    const hudPeers = Array.isArray(state.hud?.peers || state.hud?.players)
      ? (state.hud.peers || state.hud.players)
      : [];

    const allSlots = [
      {
        peerId: state.localPeerId,
        name: state.localName,
        skinId: state.localSkin,
        self: true,
        transport: state.mode === 'offline' ? 'LOCAL' : 'YOU',
        rttMs: 0,
      },
    ];
    for (const p of Array.from(net.peers.values()).sort((a, b) => a.peerId - b.peerId)) {
      allSlots.push({ ...p, self: false });
    }

    rosterEl.innerHTML = allSlots
      .map((slot) => {
        const hudEntry =
          slot.self && state.hud
            ? state.hud
            : hudPeers.find((x) => Number(x.peerId ?? x.id ?? x.slot) === slot.peerId);
        const hp = Math.round(hudEntry?.hp ?? hudEntry?.health ?? 100);
        const armor = Math.round(hudEntry?.armor ?? hudEntry?.armour ?? 0);
        const district = hudEntry?.district || hudEntry?.zone || 'Vice City';
        const inVeh = Boolean(hudEntry?.inVeh ?? hudEntry?.inVehicle);
        const vehLabel = hudEntry?.vehicleName || hudEntry?.vehicle || (inVeh ? 'In Vehicle' : 'On Foot');
        const kickBtn =
          state.isHost && !slot.self && net.connected
            ? `<button type="button" class="vc-btn danger small" data-kick-peer="${slot.peerId}">KICK</button>`
            : '';

        return `
        <div class="roster-card">
          <div class="roster-head">
            <span class="slot-tag">SLOT ${slot.peerId}</span>
            <strong>${escapeHtml(slot.name)}</strong>
            <span class="transport-pill ${slot.transport === 'P2P' ? 'p2p' : ''}">${slot.transport}${slot.rttMs ? ` ${slot.rttMs}ms` : ''}</span>
            ${kickBtn}
          </div>
          <div class="roster-meta">
            <span>HP: <b>${hp}</b></span>
            <span>ARM: <b>${armor}</b></span>
            <span>LOC: <b>${escapeHtml(district)}</b></span>
            <span>${escapeHtml(vehLabel)}</span>
            <span>${escapeHtml(getSkinLabel(slot.skinId))}</span>
          </div>
        </div>`;
      })
      .join('');

    rosterEl.querySelectorAll('[data-kick-peer]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const pid = clampInt(btn.getAttribute('data-kick-peer'), 2, 4, 0);
        if (pid >= 2) net.kickPeer(pid);
      });
    });
  }

  function renderNetChat() {
    const logEl = document.getElementById('net-chat-log');
    if (!logEl) return;
    const msgs = state.pagerMessages.slice(-20);
    logEl.innerHTML = msgs
      .map((m) => `<div class="chat-line ${m.tone}"><span>[${m.time}]</span> ${escapeHtml(m.text)}</div>`)
      .join('');
    logEl.scrollTop = logEl.scrollHeight;
  }

  function renderViceNetPanel() {
    const connBadge = document.getElementById('net-connection-badge');
    const leaveBtn = document.getElementById('net-leave-btn');
    const hostControls = document.getElementById('net-host-only-controls');
    if (connBadge) {
      if (state.mode === 'offline') {
        connBadge.textContent = 'OFFLINE (SOLO)';
      } else if (net.connected) {
        connBadge.textContent = `${state.isHost ? 'HOSTING' : 'CONNECTED'} • ROOM #${state.roomPort} • SLOT ${state.localPeerId}/4`;
      } else {
        connBadge.textContent = 'CONNECTING TO VICE NET...';
      }
    }
    if (leaveBtn) leaveBtn.disabled = state.mode === 'offline' && !net.connected;
    if (hostControls) hostControls.hidden = !state.isHost;

    syncHostTogglesUI();
    renderViceNetRoster();
    renderViceNetRooms();
    renderNetChat();
  }

  function syncHostTogglesUI() {
    const islandsToggle = document.getElementById('net-open-islands-toggle');
    if (islandsToggle) islandsToggle.checked = Boolean(state.openIslands);
  }

  function renderIdentityPreview() {
    const nameInput = document.getElementById('id-player-name');
    const skinSelect = document.getElementById('id-skin-select');
    const previewTitle = document.getElementById('id-preview-name');
    const previewSub = document.getElementById('id-preview-meta');
    if (nameInput && document.activeElement !== nameInput) nameInput.value = state.localName;
    if (skinSelect && document.activeElement !== skinSelect) skinSelect.value = String(state.localSkin);

    const modelId = skinSelect ? clampInt(skinSelect.value, 0, 129, state.localSkin) : state.localSkin;
    const modelObj = CHARACTER_MODELS.find((m) => m.id === modelId);
    if (previewTitle) previewTitle.textContent = (nameInput ? nameInput.value : state.localName) || 'Tommy';
    if (previewSub) {
      const groupName = modelObj ? modelObj.group : 'Custom Ped';
      const modelName = modelObj ? modelObj.name : `Model #${modelId}`;
      previewSub.textContent = `${modelName} (ID ${modelId}) • ${groupName} • Synced across Vice Net`;
    }
  }

  function setSavesStatus(msg, isError = false) {
    const el = document.getElementById('saves-status');
    if (!el) return;
    el.textContent = msg || '';
    el.className = `status-line ${isError ? 'err' : 'ok'}`;
    el.hidden = !msg;
  }

  function refreshSafehouseCutsBanner() {
    const banner = document.getElementById('safehouse-cuts-banner');
    if (!banner) return;
    const mounted = Boolean(window.GtaAssetLoader && window.GtaAssetLoader.isPackMounted('cuts'));
    banner.hidden = mounted || state.coopMode !== 'story_coop';
  }

  function renderSavesPanel() {
    const loggedOutBox = document.getElementById('auth-logged-out');
    const loggedInBox = document.getElementById('auth-logged-in');
    const userLabel = document.getElementById('auth-user-label');
    const listEl = document.getElementById('cloud-saves-list');
    const saveBtn = document.getElementById('save-new-campaign-btn');

    const signedIn = Boolean(state.authUser);
    if (loggedOutBox) loggedOutBox.hidden = signedIn;
    if (loggedInBox) loggedInBox.hidden = !signedIn;
    if (userLabel) userLabel.textContent = state.authUser ? state.authUser.username : '';
    if (saveBtn) saveBtn.disabled = !signedIn;

    if (!listEl) return;
    if (!signedIn) {
      listEl.innerHTML = '<div class="empty-note">Sign in above to manage Cloudflare SQLite cloud saves.</div>';
      return;
    }
    if (state.campaigns.length === 0) {
      listEl.innerHTML = '<div class="empty-note">No safehouse campaigns saved yet.</div>';
      return;
    }

    const canLoad = state.isHost || state.mode === 'offline';
    listEl.innerHTML = state.campaigns
      .map((c) => {
        const slotsDesc = Array.isArray(c.slots)
          ? c.slots
              .filter((s) => s.character_name)
              .slice(0, 4)
              .map((s) => `${s.character_name} ($${s.money ?? 0})`)
              .join(' • ')
          : '';
        return `
        <div class="save-card">
          <div class="save-card-main">
            <strong>${escapeHtml(c.title)}</strong>
            <div class="room-sub">Rev ${c.save_rev} • ${escapeHtml(c.mode)} • Updated ${escapeHtml(formatTimestamp(c.updated_at))}</div>
            <div class="room-sub">${escapeHtml(slotsDesc)}</div>
          </div>
          <div class="save-card-actions">
            <button type="button" class="vc-btn small" data-save-overwrite="${escapeHtml(c.campaign_id)}">OVERWRITE</button>
            <button type="button" class="vc-btn accent small" data-save-load="${escapeHtml(c.campaign_id)}" ${canLoad ? '' : 'disabled'}>LOAD</button>
            <button type="button" class="vc-btn danger small" data-save-del="${escapeHtml(c.campaign_id)}">DEL</button>
          </div>
        </div>`;
      })
      .join('');

    listEl.querySelectorAll('[data-save-overwrite]').forEach((btn) => {
      btn.addEventListener('click', () => saveCurrentCampaign(btn.getAttribute('data-save-overwrite')));
    });
    listEl.querySelectorAll('[data-save-load]').forEach((btn) => {
      btn.addEventListener('click', () => loadCloudCampaign(btn.getAttribute('data-save-load')));
    });
    listEl.querySelectorAll('[data-save-del]').forEach((btn) => {
      btn.addEventListener('click', () => deleteCloudCampaign(btn.getAttribute('data-save-del')));
    });
  }

  function renderCheatsAuthorityBanner() {
    const noteEl = document.getElementById('cheats-authority-note');
    const isAuth = state.isHost || state.mode === 'offline';
    if (noteEl) {
      noteEl.hidden = isAuth;
    }
    document.querySelectorAll('[data-cheat-id]').forEach((btn) => {
      btn.classList.toggle('readonly-dim', !isAuth);
    });
  }

  function syncFrontendSettingsFromEngine() {
    const raw = bridge.mpGetFrontendSettings();
    if (raw && typeof raw === 'object') {
      const aliases = {
        music_vol: ['music_vol', 'musicVolume', 'musicVol', 'm_PrefsMusicVolume'],
        sfx_vol: ['sfx_vol', 'sfxVolume', 'sfxVol', 'm_PrefsSfxVolume'],
        radio_station: ['radio_station', 'radioStation', 'radio', 'm_PrefsRadioStation'],
        brightness: ['brightness', 'm_PrefsBrightness'],
        draw_dist: ['draw_dist', 'drawDistance', 'lod', 'm_PrefsLOD'],
        subtitles: ['subtitles', 'showSubtitles', 'm_PrefsShowSubtitles'],
        widescreen: ['widescreen', 'm_PrefsUseWideScreen'],
        invert_y: ['invert_y', 'invertMouseY', 'invertLook', 'm_PrefsInvertVertically'],
        control_method: ['control_method', 'controlMethod', 'm_PrefsControlMethod'],
        show_hud: ['show_hud', 'showHud', 'hud', 'm_PrefsShowHud'],
      };
      for (const [canon, candidates] of Object.entries(aliases)) {
        for (const k of candidates) {
          if (raw[k] !== undefined) {
            state.frontendKeyMap[canon] = k;
            state.frontendSettings[canon] = Number(raw[k]);
            break;
          }
        }
      }
    }
    syncOptionsInputs();
  }

  function syncOptionsInputs() {
    const s = state.frontendSettings;
    const setVal = (id, v) => {
      const el = document.getElementById(id);
      if (el) el.value = String(v);
    };
    const setChk = (id, v) => {
      const el = document.getElementById(id);
      if (el) el.checked = Boolean(Number(v));
    };
    setVal('opt-music-vol', s.music_vol);
    setVal('opt-sfx-vol', s.sfx_vol);
    setVal('opt-radio-station', s.radio_station);
    setVal('opt-brightness', s.brightness);
    setVal('opt-draw-dist', s.draw_dist);
    setVal('opt-control-method', s.control_method);
    setChk('opt-subtitles', s.subtitles);
    setChk('opt-widescreen', s.widescreen);
    setChk('opt-invert-y', s.invert_y);
    setChk('opt-show-hud', s.show_hud);

    const mv = document.getElementById('opt-music-val');
    const sv = document.getElementById('opt-sfx-val');
    const bv = document.getElementById('opt-brightness-val');
    const dv = document.getElementById('opt-draw-val');
    if (mv) mv.textContent = String(Math.round((s.music_vol / 127) * 100)) + '%';
    if (sv) sv.textContent = String(Math.round((s.sfx_vol / 127) * 100)) + '%';
    if (bv) bv.textContent = String(s.brightness);
    if (dv) dv.textContent = String(s.draw_dist);
  }

  async function refreshCacheStatusUI() {
    const coreBadge = document.getElementById('opt-core-cache-status');
    const cutsBadge = document.getElementById('opt-cuts-cache-status');
    const cutsBtn = document.getElementById('opt-load-cuts-btn');
    if (!window.GtaAssetLoader) return;

    const coreCached = await window.GtaAssetLoader.isPackCached('core');
    const cutsCached = await window.GtaAssetLoader.isPackCached('cuts');
    const cutsMounted = window.GtaAssetLoader.isPackMounted('cuts');

    if (coreBadge) {
      coreBadge.textContent = coreCached ? 'CACHED IN BROWSER (INSTANT)' : 'NOT CACHED';
    }
    if (cutsBadge) {
      cutsBadge.textContent = cutsMounted
        ? 'MOUNTED IN GAME'
        : cutsCached
        ? 'CACHED (CLICK TO MOUNT)'
        : 'NOT DOWNLOADED (~85 MiB)';
    }
    if (cutsBtn) {
      cutsBtn.disabled = cutsMounted;
      cutsBtn.textContent = cutsMounted ? 'CUTSCENE PACK READY' : 'DOWNLOAD & MOUNT CUTSCENES';
    }
  }

  function syncAllPanelsFromState() {
    renderViceNetPanel();
    renderIdentityPreview();
    renderSavesPanel();
    renderCheatsAuthorityBanner();
    syncOptionsInputs();
    refreshSafehouseCutsBanner();
  }

  // ============================================================================
  // Populate Select Catalogs & Wire DOM Events
  // ============================================================================
  function populateSelectGroups(selectEl, items, defaultVal) {
    if (!selectEl) return;
    const grouped = new Map();
    for (const item of items) {
      if (!grouped.has(item.group)) grouped.set(item.group, []);
      grouped.get(item.group).push(item);
    }
    selectEl.innerHTML = '';
    for (const [groupName, list] of grouped.entries()) {
      const og = document.createElement('optgroup');
      og.label = groupName;
      for (const item of list) {
        const opt = document.createElement('option');
        opt.value = String(item.id);
        opt.textContent = `${item.name} (#${item.id})`;
        og.appendChild(opt);
      }
      selectEl.appendChild(og);
    }
    if (defaultVal !== undefined) {
      selectEl.value = String(defaultVal);
    }
  }

  function applyBootModeSelection(mode) {
    state.mode = mode;
    state.isHost = mode !== 'client';
    document.querySelectorAll('[data-boot-mode]').forEach((b) => {
      b.classList.toggle('active', b.getAttribute('data-boot-mode') === mode);
    });
    const modeFields = document.getElementById('boot-mode-fields');
    const hostFields = document.getElementById('boot-host-fields');
    const portWrap = document.getElementById('boot-port-wrap');
    const roomsBox = document.getElementById('boot-rooms-box');
    if (modeFields) modeFields.hidden = mode === 'client';
    if (hostFields) hostFields.hidden = mode !== 'host';
    if (portWrap) portWrap.hidden = mode === 'offline';
    if (roomsBox) roomsBox.hidden = mode !== 'client';
    if (mode === 'client') refreshRooms();
  }

  function wireEvents() {
    // Splash mode buttons
    document.querySelectorAll('[data-boot-mode]').forEach((btn) => {
      btn.addEventListener('click', () => {
        applyBootModeSelection(btn.getAttribute('data-boot-mode'));
      });
    });

    const bootCoopMode = document.getElementById('boot-coop-mode');
    const bootOpenIslands = document.getElementById('boot-open-islands');
    if (bootCoopMode && bootOpenIslands) {
      bootCoopMode.addEventListener('change', () => {
        if (bootCoopMode.value === 'open_sandbox') {
          bootOpenIslands.checked = true;
        }
      });
    }

    const bootRefreshRooms = document.getElementById('boot-refresh-rooms');
    if (bootRefreshRooms) bootRefreshRooms.addEventListener('click', refreshRooms);

    const launchBtn = document.getElementById('boot-launch-btn');
    if (launchBtn) launchBtn.addEventListener('click', launchGame);

    // Canvas pointer lock click & cursor/focus tracking
    window.__gtavcCursorInCanvas = false;
    window.__gta3CursorInCanvas = false;
    const setCursorInCanvas = (val) => {
      window.__gtavcCursorInCanvas = val;
      window.__gta3CursorInCanvas = val;
    };

    const canvas = document.getElementById('canvas');
    if (canvas) {
      canvas.addEventListener('mouseenter', () => setCursorInCanvas(true));
      canvas.addEventListener('mousemove', () => setCursorInCanvas(true));
      canvas.addEventListener('mouseleave', () => setCursorInCanvas(false));
      canvas.addEventListener('mousedown', () => {
        setCursorInCanvas(true);
        if (window.GtaAudioStream) window.GtaAudioStream.unlock();
        if (state.booted && !state.menuOpen) {
          requestCanvasPointerLock();
        }
      });
      canvas.addEventListener('contextmenu', (e) => e.preventDefault());
      canvas.addEventListener('click', () => {
        setCursorInCanvas(true);
        if (window.GtaAudioStream) window.GtaAudioStream.unlock();
        if (state.booted && !state.menuOpen) {
          requestCanvasPointerLock();
        }
      });
    }

    document.addEventListener('mouseleave', () => setCursorInCanvas(false));
    window.addEventListener('blur', () => setCursorInCanvas(false));

    document.addEventListener('pointerlockchange', () => {
      const c = document.getElementById('canvas');
      state.pointerLocked = Boolean(c && document.pointerLockElement === c);
      updatePointerHint();
    });

    // Global key bindings
    window.addEventListener('keydown', (e) => {
      if (!state.booted) return;
      const activeIsInput = isEditableTarget(document.activeElement) || isEditableTarget(e.target);

      if (e.code === 'F9' || e.code === 'F2' || e.code === 'F1' || e.code === 'Backquote') {
        if (activeIsInput && e.code === 'Backquote') return;
        e.preventDefault();
        setMenuOpen(!state.menuOpen);
        return;
      }

      if (e.code === 'Tab' && !activeIsInput) {
        e.preventDefault();
        setMenuOpen(!state.menuOpen);
        return;
      }

      if (e.code === 'KeyM' && !state.menuOpen && !activeIsInput) {
        e.preventDefault();
        setMenuOpen(true, 'vice-net');
        return;
      }

      if (e.code === 'Escape') {
        if (state.menuOpen) {
          e.preventDefault();
          setMenuOpen(false);
        } else if (!state.pointerLocked && !activeIsInput) {
          e.preventDefault();
          setMenuOpen(true);
        }
      }
    });

    window.addEventListener('resize', resizeCanvas);
    if (window.visualViewport) {
      window.visualViewport.addEventListener('resize', resizeCanvas);
    }
    document.addEventListener('fullscreenchange', resizeCanvas);

    // Pager buttons & inactivity auto-hide interaction
    const pagerEl = document.getElementById('vice-pager');
    if (pagerEl) {
      pagerEl.addEventListener('mouseenter', () => {
        state.pagerHovered = true;
        clearPagerAutoHideTimer();
        state.pagerIdleHidden = false;
        applyPagerVisibilityDOM();
      });
      pagerEl.addEventListener('mouseleave', () => {
        state.pagerHovered = false;
        schedulePagerAutoHide();
      });
    }

    let lastMouseWakeCheck = 0;
    document.addEventListener('mousemove', (e) => {
      if (!state.booted || state.menuOpen || state.pointerLocked || !state.pagerIdleHidden) return;
      const now = performance.now();
      if (now - lastMouseWakeCheck < 120) return;
      lastMouseWakeCheck = now;
      if (e.clientY <= 90 && Math.abs(e.clientX - window.innerWidth / 2) <= 220) {
        wakePager();
      }
    });

    const pagerMinBtn = document.getElementById('pager-minimize-btn');
    if (pagerMinBtn) {
      pagerMinBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        state.pagerMinimized = !state.pagerMinimized;
        renderPager();
        wakePager();
      });
    }
    const pagerMenuBtn = document.getElementById('pager-menu-btn');
    if (pagerMenuBtn) {
      pagerMenuBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        setMenuOpen(!state.menuOpen);
        wakePager();
      });
    }

    // Pause menu header & tab switches
    const resumeBtn = document.getElementById('pause-resume-btn');
    if (resumeBtn) resumeBtn.addEventListener('click', () => setMenuOpen(false));

    document.querySelectorAll('.vc-tab-btn').forEach((btn) => {
      btn.addEventListener('click', () => switchPauseTab(btn.dataset.tab));
    });

    // Tab 1: Vice Net
    const netHostBtn = document.getElementById('net-host-btn');
    if (netHostBtn) {
      netHostBtn.addEventListener('click', () => {
        const rName = document.getElementById('net-room-name')?.value;
        const rPort = document.getElementById('net-room-port')?.value;
        const rMode = document.getElementById('net-coop-mode')?.value;
        state.coopMode = rMode === 'open_sandbox' ? 'open_sandbox' : 'story_coop';
        if (rMode === 'open_sandbox') {
          state.openIslands = true;
          bridge.mpSetOpenIslands(2);
          syncHostTogglesUI();
        }
        net.connect({
          mode: 'host',
          roomPort: rPort,
          roomName: rName,
          coopMode: rMode,
          name: state.localName,
          skinId: state.localSkin,
        });
      });
    }

    const netJoinBtn = document.getElementById('net-join-btn');
    if (netJoinBtn) {
      netJoinBtn.addEventListener('click', () => {
        const rPort = document.getElementById('net-room-port')?.value;
        net.connect({
          mode: 'client',
          roomPort: rPort,
          name: state.localName,
          skinId: state.localSkin,
        });
      });
    }

    const netCopyInviteBtn = document.getElementById('net-copy-invite-btn');
    if (netCopyInviteBtn) {
      netCopyInviteBtn.addEventListener('click', async () => {
        const portInput = document.getElementById('net-room-port');
        const port = clampInt(portInput ? portInput.value : state.roomPort, 1024, 65535, DEFAULT_ROOM_PORT);
        const url = `${location.origin}${location.pathname}?room=${port}`;
        try {
          if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
            await navigator.clipboard.writeText(url);
          } else {
            const ta = document.createElement('textarea');
            ta.value = url;
            ta.style.position = 'fixed';
            ta.style.opacity = '0';
            document.body.appendChild(ta);
            ta.select();
            document.execCommand('copy');
            ta.remove();
          }
          netCopyInviteBtn.textContent = 'COPIED!';
          setTimeout(() => {
            netCopyInviteBtn.textContent = 'Copy Invite Link';
          }, 1800);
          addPagerMessage(`INVITE LINK COPIED: ?room=${port}`, 'accent');
        } catch (_) {
          addPagerMessage(`INVITE URL: ${url}`, 'accent');
        }
      });
    }

    const netLeaveBtn = document.getElementById('net-leave-btn');
    if (netLeaveBtn) {
      netLeaveBtn.addEventListener('click', () => {
        net.disconnect(false);
        state.mode = 'offline';
        state.isHost = true;
        state.localPeerId = 1;
        bridge.mpShutdown();
        bridge.mpInit(1, true);
        bridge.mpSetPlayerInfo(1, state.localName, state.localSkin);
        renderViceNetPanel();
      });
    }

    const netRefreshRoomsBtn = document.getElementById('net-refresh-rooms-btn');
    if (netRefreshRoomsBtn) netRefreshRoomsBtn.addEventListener('click', refreshRooms);

    const openIslandsToggle = document.getElementById('net-open-islands-toggle');
    if (openIslandsToggle) {
      openIslandsToggle.addEventListener('change', () => {
        if (!state.isHost && state.mode === 'client') {
          openIslandsToggle.checked = state.openIslands;
          return;
        }
        state.openIslands = openIslandsToggle.checked;
        bridge.mpSetOpenIslands(state.openIslands);
        if (state.isHost && net.connected) {
          net.pushRoomInfoSnapshot();
          net.broadcastMeta({
            type: 'world_notice',
            openIslands: state.openIslands,
            text: state.openIslands
              ? 'Host unlocked all Vice City bridges & mainland!'
              : 'Host restored standard story bridge locks.',
          });
        }
        addPagerMessage(
          state.openIslands ? 'BRIDGES UNLOCKED: Vice Beach & Mainland open.' : 'BRIDGES: Story locks active.',
          'accent'
        );
      });
    }

    const chatForm = document.getElementById('net-chat-form');
    if (chatForm) {
      chatForm.addEventListener('submit', (e) => {
        e.preventDefault();
        const input = document.getElementById('net-chat-input');
        const text = input ? input.value.trim() : '';
        if (!text) return;
        if (input) input.value = '';
        addPagerMessage(`${state.localName}: ${text}`, 'chat');
        if (net.connected) {
          net.broadcastMeta({ type: 'chat', text });
        }
      });
    }

    // Tab 2: Identity & Skin
    const idNameInput = document.getElementById('id-player-name');
    const idSkinSelect = document.getElementById('id-skin-select');
    if (idNameInput) idNameInput.addEventListener('input', renderIdentityPreview);
    if (idSkinSelect) idSkinSelect.addEventListener('change', renderIdentityPreview);

    const applyIdBtn = document.getElementById('id-apply-btn');
    if (applyIdBtn) {
      applyIdBtn.addEventListener('click', () => {
        const newName = sanitizeName(idNameInput ? idNameInput.value : state.localName, 'Tommy');
        const newSkin = clampInt(idSkinSelect ? idSkinSelect.value : state.localSkin, 0, 129, 0);
        state.localName = newName;
        state.localSkin = newSkin;
        try {
          localStorage.setItem(LS_NAME_KEY, newName);
          localStorage.setItem(LS_SKIN_KEY, String(newSkin));
        } catch (_) {}

        bridge.mpSetPlayerInfo(state.localPeerId, newName, newSkin);
        bridge.mpExecCheat(22, newSkin, 0);

        if (net.connected) {
          net.broadcastMeta({ type: 'identity', name: newName, skinId: newSkin });
          if (state.isHost) net.pushRoomInfoSnapshot();
        }
        addPagerMessage(`IDENTITY: ${newName} • ${getSkinLabel(newSkin)}`, 'accent');
        renderIdentityPreview();
        renderPager();
      });
    }

    // Tab 3: Safehouse Saves
    const loginBtn = document.getElementById('auth-login-btn');
    const regBtn = document.getElementById('auth-register-btn');
    const logoutBtn = document.getElementById('auth-logout-btn');
    const saveNewBtn = document.getElementById('save-new-campaign-btn');
    const newGameBtn = document.getElementById('world-new-game-btn');
    const restartBtn = document.getElementById('world-restart-btn');
    const localExportBtn = document.getElementById('local-export-save-btn');
    const localImportBtn = document.getElementById('local-import-save-btn');
    const localImportFile = document.getElementById('local-import-save-file');
    const safehouseCutsBtn = document.getElementById('safehouse-load-cuts-btn');

    if (loginBtn) loginBtn.addEventListener('click', () => handleAuthSubmit(false));
    if (regBtn) regBtn.addEventListener('click', () => handleAuthSubmit(true));
    if (logoutBtn) logoutBtn.addEventListener('click', handleLogout);
    if (saveNewBtn) saveNewBtn.addEventListener('click', () => saveCurrentCampaign(''));
    if (localExportBtn) localExportBtn.addEventListener('click', exportLocalSaveFile);
    if (localImportBtn && localImportFile) {
      localImportBtn.addEventListener('click', () => localImportFile.click());
      localImportFile.addEventListener('change', () => {
        const f = localImportFile.files && localImportFile.files[0];
        if (f) importLocalSaveFile(f);
        localImportFile.value = '';
      });
    }
    if (safehouseCutsBtn) {
      safehouseCutsBtn.addEventListener('click', async () => {
        safehouseCutsBtn.disabled = true;
        safehouseCutsBtn.textContent = 'MOUNTING...';
        try {
          await mountCutscenePackInBackground();
        } catch (err) {
          addPagerMessage(`CUTSCENE PACK ERROR: ${err.message}`, 'warn');
          safehouseCutsBtn.disabled = false;
          safehouseCutsBtn.textContent = 'Download & Mount Cutscenes';
        }
      });
    }
    if (newGameBtn) {
      newGameBtn.addEventListener('click', () => {
        if (state.mode === 'client' && !state.isHost) {
          setSavesStatus('Only the Host can start a New Game for the room.', true);
          return;
        }
        bridge.mpTriggerRestart(true);
        addPagerMessage('WORLD: Started New Vice City Game.', 'accent');
        setMenuOpen(false);
      });
    }
    if (restartBtn) {
      restartBtn.addEventListener('click', () => {
        if (state.mode === 'client' && !state.isHost) {
          setSavesStatus('Only the Host can restart the room session.', true);
          return;
        }
        bridge.mpTriggerRestart(false);
        addPagerMessage('WORLD: Restarted current session.', 'accent');
        setMenuOpen(false);
      });
    }

    // Tab 4: Cheats & Spawner
    document.querySelectorAll('[data-cheat-id]').forEach((btn) => {
      btn.addEventListener('click', () => {
        if (state.mode === 'client' && !state.isHost) {
          addPagerMessage('Host authority required for world cheats.', 'warn');
          return;
        }
        const cid = Number(btn.getAttribute('data-cheat-id'));
        const p1 = Number(btn.getAttribute('data-cheat-p1') || 0);
        const p2 = Number(btn.getAttribute('data-cheat-p2') || 0);
        bridge.mpExecCheat(cid, p1, p2);
        if (btn.hasAttribute('data-cheat-toggle')) {
          if (state.cheatToggles.has(cid)) state.cheatToggles.delete(cid);
          else state.cheatToggles.add(cid);
          btn.classList.toggle('active', state.cheatToggles.has(cid));
        }
        addPagerMessage(`CHEAT EXECUTED: ${btn.textContent.trim()}`, 'accent');
      });
    });

    const spawnVehBtn = document.getElementById('spawn-vehicle-btn');
    if (spawnVehBtn) {
      spawnVehBtn.addEventListener('click', () => {
        const modelIdx = clampInt(document.getElementById('spawn-vehicle-select')?.value, 130, 236, 145);
        const dist = clampInt(document.getElementById('spawn-vehicle-dist')?.value, 3, 20, 6);
        bridge.mpSpawnVehicle(modelIdx, dist);
        const vObj = VEHICLES.find((v) => v.id === modelIdx);
        const verb = state.mode === 'client' && !state.isHost ? 'REQUESTED VEHICLE' : 'SPAWNED VEHICLE';
        addPagerMessage(`${verb}: ${vObj ? vObj.name : `#${modelIdx}`}`, 'accent');
      });
    }

    document.querySelectorAll('[data-pickup-kind]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const kind = Number(btn.getAttribute('data-pickup-kind'));
        const arg = Number(btn.getAttribute('data-pickup-arg') || 0);
        bridge.mpSpawnPickup(kind, arg);
        const verb = state.mode === 'client' && !state.isHost ? 'REQUESTED PICKUP' : 'SPAWNED PICKUP';
        addPagerMessage(`${verb}: ${btn.textContent.trim()}`, 'accent');
      });
    });

    const spawnWeaponBtn = document.getElementById('spawn-weapon-pickup-btn');
    if (spawnWeaponBtn) {
      spawnWeaponBtn.addEventListener('click', () => {
        const wId = clampInt(document.getElementById('spawn-weapon-select')?.value, 1, 36, 17);
        bridge.mpSpawnPickup(6, wId);
        const wObj = PICKUP_WEAPONS.find((w) => w.id === wId);
        const verb = state.mode === 'client' && !state.isHost ? 'REQUESTED WEAPON' : 'SPAWNED WEAPON';
        addPagerMessage(`${verb}: ${wObj ? wObj.name : `#${wId}`}`, 'accent');
      });
    }

    // Tab 5: Engine Options
    const bindRange = (id, key, labelId, fmtFn) => {
      const el = document.getElementById(id);
      if (!el) return;
      el.addEventListener('input', () => {
        const val = Number(el.value);
        state.frontendSettings[key] = val;
        const lbl = document.getElementById(labelId);
        if (lbl) lbl.textContent = fmtFn ? fmtFn(val) : String(val);
        bridge.mpSetFrontendSetting(key, val);
      });
    };
    const bindSelect = (id, key) => {
      const el = document.getElementById(id);
      if (!el) return;
      el.addEventListener('change', () => {
        const val = Number(el.value);
        state.frontendSettings[key] = val;
        bridge.mpSetFrontendSetting(key, val);
      });
    };
    const bindCheck = (id, key) => {
      const el = document.getElementById(id);
      if (!el) return;
      el.addEventListener('change', () => {
        const val = el.checked ? 1 : 0;
        state.frontendSettings[key] = val;
        bridge.mpSetFrontendSetting(key, val);
      });
    };

    bindRange('opt-music-vol', 'music_vol', 'opt-music-val', (v) => `${Math.round((v / 127) * 100)}%`);
    bindRange('opt-sfx-vol', 'sfx_vol', 'opt-sfx-val', (v) => `${Math.round((v / 127) * 100)}%`);
    bindSelect('opt-radio-station', 'radio_station');
    bindRange('opt-brightness', 'brightness', 'opt-brightness-val');
    bindRange('opt-draw-dist', 'draw_dist', 'opt-draw-val');
    bindSelect('opt-control-method', 'control_method');
    bindCheck('opt-subtitles', 'subtitles');
    bindCheck('opt-widescreen', 'widescreen');
    bindCheck('opt-invert-y', 'invert_y');
    bindCheck('opt-show-hud', 'show_hud');

    const loadCutsBtn = document.getElementById('opt-load-cuts-btn');
    if (loadCutsBtn) {
      loadCutsBtn.addEventListener('click', async () => {
        if (!window.GtaAssetLoader || !wasm.mod || !wasm.mod.FS) return;
        loadCutsBtn.disabled = true;
        loadCutsBtn.textContent = 'DOWNLOADING CUTSCENES...';
        try {
          await window.GtaAssetLoader.loadCuts(wasm.mod.FS, (info) => {
            if (info.totalBytes > 0) {
              const pct = Math.min(100, Math.round((info.loadedBytes / info.totalBytes) * 100));
              loadCutsBtn.textContent = `${info.phase.toUpperCase()} ${pct}%`;
            }
          });
          addPagerMessage('CUTSCENE PACK: anim/cuts.img mounted!', 'accent');
        } catch (err) {
          addPagerMessage(`CUTSCENE PACK ERROR: ${err.message}`, 'warn');
        }
        refreshSafehouseCutsBanner();
        await refreshCacheStatusUI();
      });
    }

    const clearCacheBtn = document.getElementById('opt-clear-cache-btn');
    if (clearCacheBtn) {
      clearCacheBtn.addEventListener('click', async () => {
        if (!window.GtaAssetLoader) return;
        await window.GtaAssetLoader.clearCache();
        addPagerMessage('VFS CACHE: Cleared IndexedDB gtavcweb_vfs_v1.', 'warn');
        await refreshCacheStatusUI();
      });
    }
  }

  // ============================================================================
  // Initialization on DOM Ready
  // ============================================================================
  async function initUI() {
    populateSelectGroups(document.getElementById('boot-skin-select'), CHARACTER_MODELS, state.localSkin);
    populateSelectGroups(document.getElementById('id-skin-select'), CHARACTER_MODELS, state.localSkin);
    populateSelectGroups(document.getElementById('spawn-vehicle-select'), VEHICLES, 145);

    const weaponSelect = document.getElementById('spawn-weapon-select');
    if (weaponSelect) {
      weaponSelect.innerHTML = PICKUP_WEAPONS.map(
        (w) => `<option value="${w.id}">${escapeHtml(w.name)} (#${w.id})</option>`
      ).join('');
      weaponSelect.value = '17';
    }

    const radioSelect = document.getElementById('opt-radio-station');
    if (radioSelect) {
      radioSelect.innerHTML = RADIO_STATIONS.map(
        (r) => `<option value="${r.id}">${escapeHtml(r.name)}</option>`
      ).join('');
    }

    const bootNameInput = document.getElementById('boot-player-name');
    if (bootNameInput) bootNameInput.value = state.localName;

    const bootRoomName = document.getElementById('boot-room-name');
    if (bootRoomName) bootRoomName.value = `${state.localName}'s Vice City`;

    const netRoomName = document.getElementById('net-room-name');
    if (netRoomName) netRoomName.value = `${state.localName}'s Vice City`;

    const bootRoomPort = document.getElementById('boot-room-port');
    if (bootRoomPort) bootRoomPort.value = String(state.roomPort);

    const netRoomPort = document.getElementById('net-room-port');
    if (netRoomPort) netRoomPort.value = String(state.roomPort);

    wireEvents();
    applyBootModeSelection(state.mode);
    resizeCanvas();

    if (window.GtaAssetLoader) {
      const bootCacheTag = document.getElementById('boot-cache-tag');
      window.GtaAssetLoader.isPackCached('core').then((cached) => {
        if (bootCacheTag) {
          bootCacheTag.textContent = cached
            ? 'BROWSER CACHE READY — INSTANT LAUNCH'
            : 'FIRST LAUNCH DOWNLOAD (CACHED IN INDEXEDDB)';
          bootCacheTag.classList.toggle('cached', cached);
        }
      });
    }

    refreshRooms();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initUI);
  } else {
    initUI();
  }

  const uiApi = {
    state,
    wasm,
    bridge,
    net,
    openMenu: (tab) => setMenuOpen(true, tab),
    closeMenu: () => setMenuOpen(false),
    addPagerMessage,
    wakePager,
  };

  window.GtaVcUI = uiApi;
  window.Gta3UI = uiApi;
})();
