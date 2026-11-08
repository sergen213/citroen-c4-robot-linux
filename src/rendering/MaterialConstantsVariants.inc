// Original material scalar writes for all finite source capability/shader branches.
// Stable source profile ID = (((SHQ * 2 + shaderVersionAtLeast2020) * 2 + cubeCapsAtLeast16) * 2 + shadowPositive).
// SHQ is GA_Shader_Seviyesi (0/1/2); CGM controls view distance and is not a shader selector.
// The full balanced Starter was selected independently for all24 profiles; no unresolved active condition or scalar expression.
// Identical numeric payloads and identical material-index buckets are deduplicated without changing source field-presence masks.
// 212 names: 208 world identities plus four original vehicle/robot color-switch controller materials.
// WDL literals use binary32 -> truncate Q10; float(literal) transports IEEE binary32(Q10/1024).
// Original color units remain unchanged, including signed and above255 values; masks distinguish zero from unassigned.
// pixel_for_vec skill1 is a DWORD: exact Q10 reflection percentage retained separately, packed conversion explicitly unknown.
// Texture quality changes resource identity separately; these original scalar constants do not select modern GPU presets.
constexpr MaterialConstants material_constant_variants[] = {
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"mat_multi_anadolu", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"mat_multi_avrupa", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=215@c4_materials.wdl:5372; ambient_green=230@c4_materials.wdl:5373; ambient_blue=255@c4_materials.wdl:5374; diffuse_red=255@c4_materials.wdl:5375
    // diffuse_green=255@c4_materials.wdl:5376; diffuse_blue=255@c4_materials.wdl:5377; specular_red=0@c4_materials.wdl:5378; specular_green=0@c4_materials.wdl:5379
    // specular_blue=0@c4_materials.wdl:5380; emissive_red=10@c4_materials.wdl:5381; emissive_green=10@c4_materials.wdl:5382; emissive_blue=16@c4_materials.wdl:5383
    // power=0@c4_materials.wdl:5384; albedo=-100@c4_materials.wdl:5385
    {"Mtl_33", {}, 0x00000u, {215.0f, 230.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {10.0f, 10.0f, 16.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=170@c4_materials.wdl:3263; ambient_green=169@c4_materials.wdl:3264; ambient_blue=166@c4_materials.wdl:3265; diffuse_red=255@c4_materials.wdl:3266
    // diffuse_green=255@c4_materials.wdl:3267; diffuse_blue=255@c4_materials.wdl:3268; specular_red=0@c4_materials.wdl:3269; specular_green=0@c4_materials.wdl:3270
    // specular_blue=0@c4_materials.wdl:3271; emissive_red=0@c4_materials.wdl:3272; emissive_green=0@c4_materials.wdl:3273; emissive_blue=0@c4_materials.wdl:3274
    // power=0@c4_materials.wdl:3275; albedo=-29@c4_materials.wdl:3276
    {"Mtl_adres", {}, 0x00000u, {170.0f, 169.0f, 166.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, -29.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:845; ambient_green=233@c4_materials.wdl:846; ambient_blue=209@c4_materials.wdl:847; diffuse_red=255@c4_materials.wdl:848
    // diffuse_green=255@c4_materials.wdl:849; diffuse_blue=255@c4_materials.wdl:850; specular_red=74@c4_materials.wdl:851; specular_green=0@c4_materials.wdl:852
    // specular_blue=18@c4_materials.wdl:853; emissive_red=0@c4_materials.wdl:854; emissive_green=0@c4_materials.wdl:855; emissive_blue=19@c4_materials.wdl:856
    // power=0@c4_materials.wdl:857; albedo=50@c4_materials.wdl:858
    {"Mtl_afis_site", {}, 0x00000u, {255.0f, 233.0f, 209.0f}, {255.0f, 255.0f, 255.0f}, {74.0f, 0.0f, 18.0f}, {0.0f, 0.0f, 19.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4326; ambient_green=255@c4_materials.wdl:4327; ambient_blue=255@c4_materials.wdl:4328; diffuse_red=255@c4_materials.wdl:4329
    // diffuse_green=255@c4_materials.wdl:4330; diffuse_blue=255@c4_materials.wdl:4331; specular_red=0@c4_materials.wdl:4332; specular_green=0@c4_materials.wdl:4333
    // specular_blue=0@c4_materials.wdl:4334; emissive_red=0@c4_materials.wdl:4335; emissive_green=0@c4_materials.wdl:4336; emissive_blue=0@c4_materials.wdl:4337
    // power=0@c4_materials.wdl:4338; albedo=47@c4_materials.wdl:4339
    {"Mtl_agac", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 47.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=244@c4_materials.wdl:5770; ambient_green=244@c4_materials.wdl:5771; ambient_blue=244@c4_materials.wdl:5772; diffuse_red=255@c4_materials.wdl:5773
    // diffuse_green=255@c4_materials.wdl:5774; diffuse_blue=255@c4_materials.wdl:5775; specular_red=0@c4_materials.wdl:5776; specular_green=0@c4_materials.wdl:5777
    // specular_blue=0@c4_materials.wdl:5778; emissive_red=7@c4_materials.wdl:5779; emissive_green=16@c4_materials.wdl:5780; emissive_blue=32@c4_materials.wdl:5781
    // power=0@c4_materials.wdl:5782; albedo=-100@c4_materials.wdl:5783
    {"Mtl_agac_don", {}, 0x00000u, {244.0f, 244.0f, 244.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {7.0f, 16.0f, 32.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=193@c4_materials.wdl:5819; ambient_green=193@c4_materials.wdl:5820; ambient_blue=193@c4_materials.wdl:5821; diffuse_red=251@c4_materials.wdl:5822
    // diffuse_green=251@c4_materials.wdl:5823; diffuse_blue=251@c4_materials.wdl:5824; specular_red=91@c4_materials.wdl:5825; specular_green=61@c4_materials.wdl:5826
    // specular_blue=58@c4_materials.wdl:5827; emissive_red=0@c4_materials.wdl:5828; emissive_green=0@c4_materials.wdl:5829; emissive_blue=0@c4_materials.wdl:5830
    // power=0@c4_materials.wdl:5831; albedo=34@c4_materials.wdl:5832
    {"Mtl_agaclar", {}, 0x00000u, {193.0f, 193.0f, 193.0f}, {251.0f, 251.0f, 251.0f}, {91.0f, 61.0f, 58.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 34.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=125@c4_materials.wdl:4228; ambient_green=131@c4_materials.wdl:4229; ambient_blue=132@c4_materials.wdl:4230; diffuse_red=255@c4_materials.wdl:4231
    // diffuse_green=255@c4_materials.wdl:4232; diffuse_blue=255@c4_materials.wdl:4233; specular_red=0@c4_materials.wdl:4234; specular_green=0@c4_materials.wdl:4235
    // specular_blue=0@c4_materials.wdl:4236; emissive_red=0@c4_materials.wdl:4237; emissive_green=0@c4_materials.wdl:4238; emissive_blue=0@c4_materials.wdl:4239
    // power=0@c4_materials.wdl:4240; albedo=50@c4_materials.wdl:4241
    {"Mtl_ara_yol", {}, 0x00000u, {125.0f, 131.0f, 132.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_araba_lastik", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=71@c4_materials.wdl:7042; ambient_green=68@c4_materials.wdl:7043; ambient_blue=76@c4_materials.wdl:7044; diffuse_red=76@c4_materials.wdl:7045
    // diffuse_green=82@c4_materials.wdl:7046; diffuse_blue=83@c4_materials.wdl:7047; specular_red=0@c4_materials.wdl:7048; specular_green=1@c4_materials.wdl:7049
    // specular_blue=0@c4_materials.wdl:7050; emissive_red=0@c4_materials.wdl:7051; emissive_green=4@c4_materials.wdl:7052; emissive_blue=1@c4_materials.wdl:7053
    // power=0@c4_materials.wdl:7054; albedo=-28@c4_materials.wdl:7055
    {"Mtl_arac_cam", {}, 0x00000u, {71.0f, 68.0f, 76.0f}, {76.0f, 82.0f, 83.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 4.0f, 1.0f}, 0xfffu, 0.0f, -28.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=39@c4_materials.wdl:6954; ambient_green=42@c4_materials.wdl:6955; ambient_blue=42@c4_materials.wdl:6956; diffuse_red=0@c4_materials.wdl:6957
    // diffuse_green=0@c4_materials.wdl:6958; diffuse_blue=0@c4_materials.wdl:6959; specular_red=137@c4_materials.wdl:6960; specular_green=87@c4_materials.wdl:6961
    // specular_blue=0@c4_materials.wdl:6962; emissive_red=0@c4_materials.wdl:6963; emissive_green=0@c4_materials.wdl:6964; emissive_blue=0@c4_materials.wdl:6965
    // power=0@c4_materials.wdl:6966; albedo=16@c4_materials.wdl:6967
    {"Mtl_arac_farcam", {}, 0x00000u, {39.0f, 42.0f, 42.0f}, {0.0f, 0.0f, 0.0f}, {137.0f, 87.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 16.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=123@c4_materials.wdl:6978; ambient_green=126@c4_materials.wdl:6979; ambient_blue=117@c4_materials.wdl:6980; diffuse_red=82@c4_materials.wdl:6981
    // diffuse_green=88@c4_materials.wdl:6982; diffuse_blue=91@c4_materials.wdl:6983; specular_red=1@c4_materials.wdl:6984; specular_green=0@c4_materials.wdl:6985
    // specular_blue=0@c4_materials.wdl:6986; emissive_red=4@c4_materials.wdl:6987; emissive_green=0@c4_materials.wdl:6988; emissive_blue=0@c4_materials.wdl:6989
    // power=0@c4_materials.wdl:6990; albedo=120@c4_materials.wdl:6991
    {"Mtl_arac_fargovde", {}, 0x00000u, {123.0f, 126.0f, 117.0f}, {82.0f, 88.0f, 91.0f}, {1.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 120.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=163@c4_materials.wdl:2518; ambient_green=163@c4_materials.wdl:2519; ambient_blue=163@c4_materials.wdl:2520; diffuse_red=167@c4_materials.wdl:2521
    // diffuse_green=175@c4_materials.wdl:2522; diffuse_blue=177@c4_materials.wdl:2523; specular_red=0@c4_materials.wdl:2524; specular_green=0@c4_materials.wdl:2525
    // specular_blue=0@c4_materials.wdl:2526; emissive_red=12@c4_materials.wdl:2527; emissive_green=12@c4_materials.wdl:2528; emissive_blue=12@c4_materials.wdl:2529
    // power=0@c4_materials.wdl:2530; albedo=50@c4_materials.wdl:2531
    {"Mtl_arac_koltuk", {}, 0x00000u, {163.0f, 163.0f, 163.0f}, {167.0f, 175.0f, 177.0f}, {0.0f, 0.0f, 0.0f}, {12.0f, 12.0f, 12.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=136@c4_materials.wdl:3992; ambient_green=129@c4_materials.wdl:3993; ambient_blue=129@c4_materials.wdl:3994; diffuse_red=138@c4_materials.wdl:3995
    // diffuse_green=138@c4_materials.wdl:3996; diffuse_blue=138@c4_materials.wdl:3997; specular_red=0@c4_materials.wdl:3998; specular_green=0@c4_materials.wdl:3999
    // specular_blue=0@c4_materials.wdl:4000; emissive_red=0@c4_materials.wdl:4001; emissive_green=0@c4_materials.wdl:4002; emissive_blue=0@c4_materials.wdl:4003
    // power=0@c4_materials.wdl:4004; albedo=30@c4_materials.wdl:4005
    {"Mtl_arazii", {}, 0x00000u, {136.0f, 129.0f, 129.0f}, {138.0f, 138.0f, 138.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 30.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:648; ambient_green=145@c4_materials.wdl:649; ambient_blue=145@c4_materials.wdl:650; diffuse_red=255@c4_materials.wdl:651
    // diffuse_green=255@c4_materials.wdl:652; diffuse_blue=255@c4_materials.wdl:653; specular_red=0@c4_materials.wdl:654; specular_green=0@c4_materials.wdl:655
    // specular_blue=0@c4_materials.wdl:656; emissive_red=10@c4_materials.wdl:657; emissive_green=10@c4_materials.wdl:658; emissive_blue=9@c4_materials.wdl:659
    // power=0@c4_materials.wdl:660; albedo=-20@c4_materials.wdl:661
    {"Mtl_arka_far", {}, 0x00000u, {145.0f, 145.0f, 145.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {10.0f, 10.0f, 9.0f}, 0xfffu, 0.0f, -20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:2765; ambient_green=239@c4_materials.wdl:2766; ambient_blue=203@c4_materials.wdl:2767; diffuse_red=68@c4_materials.wdl:2768
    // diffuse_green=68@c4_materials.wdl:2769; diffuse_blue=68@c4_materials.wdl:2770; specular_red=68@c4_materials.wdl:2771; specular_green=70@c4_materials.wdl:2772
    // specular_blue=70@c4_materials.wdl:2773; emissive_red=4@c4_materials.wdl:2774; emissive_green=4@c4_materials.wdl:2775; emissive_blue=4@c4_materials.wdl:2776
    // power=0@c4_materials.wdl:2777; albedo=-100@c4_materials.wdl:2778
    {"Mtl_arka_orman", {}, 0x00000u, {255.0f, 239.0f, 203.0f}, {68.0f, 68.0f, 68.0f}, {68.0f, 70.0f, 70.0f}, {4.0f, 4.0f, 4.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=38@c4_materials.wdl:2501; ambient_green=36@c4_materials.wdl:2502; ambient_blue=36@c4_materials.wdl:2503; diffuse_red=230@c4_materials.wdl:2504
    // diffuse_green=233@c4_materials.wdl:2505; diffuse_blue=233@c4_materials.wdl:2506; specular_red=0@c4_materials.wdl:2507; specular_green=0@c4_materials.wdl:2508
    // specular_blue=0@c4_materials.wdl:2509; emissive_red=0@c4_materials.wdl:2510; emissive_green=0@c4_materials.wdl:2511; emissive_blue=0@c4_materials.wdl:2512
    // power=0@c4_materials.wdl:2513; albedo=-24@c4_materials.wdl:2514
    {"Mtl_arti", {}, 0x00000u, {38.0f, 36.0f, 36.0f}, {230.0f, 233.0f, 233.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, -24.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=111@c4_materials.wdl:892; ambient_green=105@c4_materials.wdl:893; ambient_blue=119@c4_materials.wdl:894; diffuse_red=255@c4_materials.wdl:895
    // diffuse_green=255@c4_materials.wdl:896; diffuse_blue=255@c4_materials.wdl:897; specular_red=32@c4_materials.wdl:898; specular_green=33@c4_materials.wdl:899
    // specular_blue=27@c4_materials.wdl:900; emissive_red=0@c4_materials.wdl:901; emissive_green=0@c4_materials.wdl:902; emissive_blue=0@c4_materials.wdl:903
    // power=0@c4_materials.wdl:904; albedo=20@c4_materials.wdl:905
    {"Mtl_askagel", {}, 0x00000u, {111.0f, 105.0f, 119.0f}, {255.0f, 255.0f, 255.0f}, {32.0f, 33.0f, 27.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=178@c4_materials.wdl:1374; ambient_green=148@c4_materials.wdl:1375; ambient_blue=138@c4_materials.wdl:1376; diffuse_red=54@c4_materials.wdl:1377
    // diffuse_green=58@c4_materials.wdl:1378; diffuse_blue=50@c4_materials.wdl:1379; specular_red=3@c4_materials.wdl:1380; specular_green=7@c4_materials.wdl:1381
    // specular_blue=7@c4_materials.wdl:1382; emissive_red=0@c4_materials.wdl:1383; emissive_green=0@c4_materials.wdl:1384; emissive_blue=0@c4_materials.wdl:1385
    // power=0@c4_materials.wdl:1386; albedo=131@c4_materials.wdl:1387
    {"Mtl_bal_kil", {}, 0x00000u, {178.0f, 148.0f, 138.0f}, {54.0f, 58.0f, 50.0f}, {3.0f, 7.0f, 7.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 131.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=132@c4_materials.wdl:986; ambient_green=110@c4_materials.wdl:987; ambient_blue=102@c4_materials.wdl:988; diffuse_red=54@c4_materials.wdl:989
    // diffuse_green=58@c4_materials.wdl:990; diffuse_blue=50@c4_materials.wdl:991; specular_red=3@c4_materials.wdl:992; specular_green=7@c4_materials.wdl:993
    // specular_blue=7@c4_materials.wdl:994; emissive_red=0@c4_materials.wdl:995; emissive_green=0@c4_materials.wdl:996; emissive_blue=0@c4_materials.wdl:997
    // power=0@c4_materials.wdl:998; albedo=131@c4_materials.wdl:999
    {"Mtl_bal_kopek", {}, 0x00000u, {132.0f, 110.0f, 102.0f}, {54.0f, 58.0f, 50.0f}, {3.0f, 7.0f, 7.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 131.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:5137; ambient_green=255@c4_materials.wdl:5138; ambient_blue=255@c4_materials.wdl:5139; diffuse_red=255@c4_materials.wdl:5140
    // diffuse_green=255@c4_materials.wdl:5141; diffuse_blue=255@c4_materials.wdl:5142; specular_red=0@c4_materials.wdl:5143; specular_green=0@c4_materials.wdl:5144
    // specular_blue=0@c4_materials.wdl:5145; emissive_red=0@c4_materials.wdl:5146; emissive_green=0@c4_materials.wdl:5147; emissive_blue=0@c4_materials.wdl:5148
    // power=0@c4_materials.wdl:5149; albedo=50@c4_materials.wdl:5150
    {"Mtl_balon", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=105@c4_materials.wdl:3246; ambient_green=105@c4_materials.wdl:3247; ambient_blue=129@c4_materials.wdl:3248; diffuse_red=175@c4_materials.wdl:3249
    // diffuse_green=175@c4_materials.wdl:3250; diffuse_blue=175@c4_materials.wdl:3251; specular_red=0@c4_materials.wdl:3252; specular_green=0@c4_materials.wdl:3253
    // specular_blue=0@c4_materials.wdl:3254; emissive_red=0@c4_materials.wdl:3255; emissive_green=0@c4_materials.wdl:3256; emissive_blue=0@c4_materials.wdl:3257
    // power=0@c4_materials.wdl:3258; albedo=29@c4_materials.wdl:3259
    {"Mtl_bank", {}, 0x00000u, {105.0f, 105.0f, 129.0f}, {175.0f, 175.0f, 175.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 29.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_bariyer", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=102@c4_materials.wdl:6381; ambient_green=102@c4_materials.wdl:6382; ambient_blue=102@c4_materials.wdl:6383; diffuse_red=99@c4_materials.wdl:6384
    // diffuse_green=99@c4_materials.wdl:6385; diffuse_blue=99@c4_materials.wdl:6386; specular_red=22@c4_materials.wdl:6387; specular_green=22@c4_materials.wdl:6388
    // specular_blue=22@c4_materials.wdl:6389; emissive_red=33@c4_materials.wdl:6390; emissive_green=33@c4_materials.wdl:6391; emissive_blue=33@c4_materials.wdl:6392
    // power=0@c4_materials.wdl:6393; albedo=200@c4_materials.wdl:6394
    {"Mtl_bas_1", {}, 0x00000u, {102.0f, 102.0f, 102.0f}, {99.0f, 99.0f, 99.0f}, {22.0f, 22.0f, 22.0f}, {33.0f, 33.0f, 33.0f}, 0xfffu, 0.0f, 200.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=200@c4_materials.wdl:5083; ambient_green=210@c4_materials.wdl:5084; ambient_blue=220@c4_materials.wdl:5085; diffuse_red=220@c4_materials.wdl:5086
    // diffuse_green=220@c4_materials.wdl:5087; diffuse_blue=220@c4_materials.wdl:5088; specular_red=19@c4_materials.wdl:5089; specular_green=18@c4_materials.wdl:5090
    // specular_blue=21@c4_materials.wdl:5091; emissive_red=0@c4_materials.wdl:5092; emissive_green=0@c4_materials.wdl:5093; emissive_blue=0@c4_materials.wdl:5094
    // power=0@c4_materials.wdl:5095; albedo=20@c4_materials.wdl:5096
    {"Mtl_bayrak", {}, 0x00000u, {200.0f, 210.0f, 220.0f}, {220.0f, 220.0f, 220.0f}, {19.0f, 18.0f, 21.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=116@c4_materials.wdl:700; ambient_green=106@c4_materials.wdl:701; ambient_blue=103@c4_materials.wdl:702; diffuse_red=244@c4_materials.wdl:703
    // diffuse_green=232@c4_materials.wdl:704; diffuse_blue=209@c4_materials.wdl:705; specular_red=74@c4_materials.wdl:706; specular_green=65@c4_materials.wdl:707
    // specular_blue=62@c4_materials.wdl:708; emissive_red=15@c4_materials.wdl:709; emissive_green=16@c4_materials.wdl:710; emissive_blue=16@c4_materials.wdl:711
    // power=0@c4_materials.wdl:712; albedo=7@c4_materials.wdl:713
    {"Mtl_bayrak2", {}, 0x00000u, {116.0f, 106.0f, 103.0f}, {244.0f, 232.0f, 209.0f}, {74.0f, 65.0f, 62.0f}, {15.0f, 16.0f, 16.0f}, 0xfffu, 0.0f, 7.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=163@c4_materials.wdl:2413; ambient_green=199@c4_materials.wdl:2414; ambient_blue=212@c4_materials.wdl:2415; diffuse_red=255@c4_materials.wdl:2416
    // diffuse_green=255@c4_materials.wdl:2417; diffuse_blue=255@c4_materials.wdl:2418; specular_red=80@c4_materials.wdl:2419; specular_green=58@c4_materials.wdl:2420
    // specular_blue=0@c4_materials.wdl:2421; emissive_red=12@c4_materials.wdl:2422; emissive_green=12@c4_materials.wdl:2423; emissive_blue=12@c4_materials.wdl:2424
    // power=0@c4_materials.wdl:2425; albedo=20@c4_materials.wdl:2426
    {"Mtl_beko", {}, 0x00000u, {163.0f, 199.0f, 212.0f}, {255.0f, 255.0f, 255.0f}, {80.0f, 58.0f, 0.0f}, {12.0f, 12.0f, 12.0f}, 0xfffu, 0.0f, 20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_bez", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=200@c4_materials.wdl:3926; ambient_green=200@c4_materials.wdl:3927; ambient_blue=200@c4_materials.wdl:3928; diffuse_red=151@c4_materials.wdl:3929
    // diffuse_green=123@c4_materials.wdl:3930; diffuse_blue=154@c4_materials.wdl:3931; specular_red=7@c4_materials.wdl:3932; specular_green=7@c4_materials.wdl:3933
    // specular_blue=7@c4_materials.wdl:3934; emissive_red=0@c4_materials.wdl:3935; emissive_green=0@c4_materials.wdl:3936; emissive_blue=0@c4_materials.wdl:3937
    // power=0@c4_materials.wdl:3938; albedo=113@c4_materials.wdl:3939
    {"Mtl_bez_kenar", {}, 0x00000u, {200.0f, 200.0f, 200.0f}, {151.0f, 123.0f, 154.0f}, {7.0f, 7.0f, 7.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 113.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=178@c4_materials.wdl:4632; ambient_green=181@c4_materials.wdl:4633; ambient_blue=183@c4_materials.wdl:4634; diffuse_red=255@c4_materials.wdl:4635
    // diffuse_green=255@c4_materials.wdl:4636; diffuse_blue=255@c4_materials.wdl:4637; specular_red=13@c4_materials.wdl:4638; specular_green=12@c4_materials.wdl:4639
    // specular_blue=20@c4_materials.wdl:4640; emissive_red=0@c4_materials.wdl:4641; emissive_green=0@c4_materials.wdl:4642; emissive_blue=0@c4_materials.wdl:4643
    // power=0@c4_materials.wdl:4644; albedo=23@c4_materials.wdl:4645
    {"Mtl_bin7", {}, 0x00000u, {178.0f, 181.0f, 183.0f}, {255.0f, 255.0f, 255.0f}, {13.0f, 12.0f, 20.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 23.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=135@c4_materials.wdl:4564; ambient_green=137@c4_materials.wdl:4565; ambient_blue=143@c4_materials.wdl:4566; diffuse_red=255@c4_materials.wdl:4567
    // diffuse_green=255@c4_materials.wdl:4568; diffuse_blue=255@c4_materials.wdl:4569; specular_red=0@c4_materials.wdl:4570; specular_green=0@c4_materials.wdl:4571
    // specular_blue=10@c4_materials.wdl:4572; emissive_red=0@c4_materials.wdl:4573; emissive_green=0@c4_materials.wdl:4574; emissive_blue=0@c4_materials.wdl:4575
    // power=0@c4_materials.wdl:4576; albedo=20@c4_materials.wdl:4577
    {"Mtl_bin_10", {}, 0x00000u, {135.0f, 137.0f, 143.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=174@c4_materials.wdl:4598; ambient_green=178@c4_materials.wdl:4599; ambient_blue=178@c4_materials.wdl:4600; diffuse_red=255@c4_materials.wdl:4601
    // diffuse_green=255@c4_materials.wdl:4602; diffuse_blue=255@c4_materials.wdl:4603; specular_red=15@c4_materials.wdl:4604; specular_green=15@c4_materials.wdl:4605
    // specular_blue=15@c4_materials.wdl:4606; emissive_red=0@c4_materials.wdl:4607; emissive_green=0@c4_materials.wdl:4608; emissive_blue=0@c4_materials.wdl:4609
    // power=0@c4_materials.wdl:4610; albedo=50@c4_materials.wdl:4611
    {"Mtl_bin_10_lam", {}, 0x00000u, {174.0f, 178.0f, 178.0f}, {255.0f, 255.0f, 255.0f}, {15.0f, 15.0f, 15.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=149@c4_materials.wdl:4194; ambient_green=149@c4_materials.wdl:4195; ambient_blue=151@c4_materials.wdl:4196; diffuse_red=245@c4_materials.wdl:4197
    // diffuse_green=254@c4_materials.wdl:4198; diffuse_blue=250@c4_materials.wdl:4199; specular_red=25@c4_materials.wdl:4200; specular_green=25@c4_materials.wdl:4201
    // specular_blue=25@c4_materials.wdl:4202; emissive_red=18@c4_materials.wdl:4203; emissive_green=15@c4_materials.wdl:4204; emissive_blue=15@c4_materials.wdl:4205
    // power=0@c4_materials.wdl:4206; albedo=63@c4_materials.wdl:4207
    {"Mtl_bin_12", {}, 0x00000u, {149.0f, 149.0f, 151.0f}, {245.0f, 254.0f, 250.0f}, {25.0f, 25.0f, 25.0f}, {18.0f, 15.0f, 15.0f}, 0xfffu, 0.0f, 63.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=164@c4_materials.wdl:4160; ambient_green=166@c4_materials.wdl:4161; ambient_blue=163@c4_materials.wdl:4162; diffuse_red=255@c4_materials.wdl:4163
    // diffuse_green=255@c4_materials.wdl:4164; diffuse_blue=255@c4_materials.wdl:4165; specular_red=0@c4_materials.wdl:4166; specular_green=0@c4_materials.wdl:4167
    // specular_blue=0@c4_materials.wdl:4168; emissive_red=0@c4_materials.wdl:4169; emissive_green=0@c4_materials.wdl:4170; emissive_blue=0@c4_materials.wdl:4171
    // power=0@c4_materials.wdl:4172; albedo=50@c4_materials.wdl:4173
    {"Mtl_bin_12_b", {}, 0x00000u, {164.0f, 166.0f, 163.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=201@c4_materials.wdl:4126; ambient_green=201@c4_materials.wdl:4127; ambient_blue=201@c4_materials.wdl:4128; diffuse_red=250@c4_materials.wdl:4129
    // diffuse_green=255@c4_materials.wdl:4130; diffuse_blue=255@c4_materials.wdl:4131; specular_red=0@c4_materials.wdl:4132; specular_green=0@c4_materials.wdl:4133
    // specular_blue=0@c4_materials.wdl:4134; emissive_red=0@c4_materials.wdl:4135; emissive_green=0@c4_materials.wdl:4136; emissive_blue=0@c4_materials.wdl:4137
    // power=0@c4_materials.wdl:4138; albedo=50@c4_materials.wdl:4139
    {"Mtl_bin_13", {}, 0x00000u, {201.0f, 201.0f, 201.0f}, {250.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=141@c4_materials.wdl:4092; ambient_green=143@c4_materials.wdl:4093; ambient_blue=143@c4_materials.wdl:4094; diffuse_red=255@c4_materials.wdl:4095
    // diffuse_green=255@c4_materials.wdl:4096; diffuse_blue=255@c4_materials.wdl:4097; specular_red=0@c4_materials.wdl:4098; specular_green=0@c4_materials.wdl:4099
    // specular_blue=0@c4_materials.wdl:4100; emissive_red=0@c4_materials.wdl:4101; emissive_green=0@c4_materials.wdl:4102; emissive_blue=0@c4_materials.wdl:4103
    // power=0@c4_materials.wdl:4104; albedo=135@c4_materials.wdl:4105
    {"Mtl_bin_14", {}, 0x00000u, {141.0f, 143.0f, 143.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 135.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=167@c4_materials.wdl:4666; ambient_green=167@c4_materials.wdl:4667; ambient_blue=169@c4_materials.wdl:4668; diffuse_red=163@c4_materials.wdl:4669
    // diffuse_green=170@c4_materials.wdl:4670; diffuse_blue=180@c4_materials.wdl:4671; specular_red=154@c4_materials.wdl:4672; specular_green=154@c4_materials.wdl:4673
    // specular_blue=154@c4_materials.wdl:4674; emissive_red=0@c4_materials.wdl:4675; emissive_green=0@c4_materials.wdl:4676; emissive_blue=0@c4_materials.wdl:4677
    // power=0@c4_materials.wdl:4678; albedo=-100@c4_materials.wdl:4679
    {"Mtl_bin_8", {}, 0x00000u, {167.0f, 167.0f, 169.0f}, {163.0f, 170.0f, 180.0f}, {154.0f, 154.0f, 154.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4734; ambient_green=255@c4_materials.wdl:4735; ambient_blue=255@c4_materials.wdl:4736; diffuse_red=255@c4_materials.wdl:4737
    // diffuse_green=255@c4_materials.wdl:4738; diffuse_blue=255@c4_materials.wdl:4739; specular_red=0@c4_materials.wdl:4740; specular_green=0@c4_materials.wdl:4741
    // specular_blue=0@c4_materials.wdl:4742; emissive_red=0@c4_materials.wdl:4743; emissive_green=0@c4_materials.wdl:4744; emissive_blue=0@c4_materials.wdl:4745
    // power=0@c4_materials.wdl:4746; albedo=50@c4_materials.wdl:4747
    {"Mtl_bin_9", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=180@c4_materials.wdl:4700; ambient_green=180@c4_materials.wdl:4701; ambient_blue=180@c4_materials.wdl:4702; diffuse_red=15@c4_materials.wdl:4703
    // diffuse_green=15@c4_materials.wdl:4704; diffuse_blue=15@c4_materials.wdl:4705; specular_red=0@c4_materials.wdl:4706; specular_green=0@c4_materials.wdl:4707
    // specular_blue=0@c4_materials.wdl:4708; emissive_red=0@c4_materials.wdl:4709; emissive_green=0@c4_materials.wdl:4710; emissive_blue=0@c4_materials.wdl:4711
    // power=0@c4_materials.wdl:4712; albedo=50@c4_materials.wdl:4713
    {"Mtl_bin_alt", {}, 0x00000u, {180.0f, 180.0f, 180.0f}, {15.0f, 15.0f, 15.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=227@c4_materials.wdl:2950; ambient_green=212@c4_materials.wdl:2951; ambient_blue=212@c4_materials.wdl:2952; diffuse_red=255@c4_materials.wdl:2953
    // diffuse_green=250@c4_materials.wdl:2954; diffuse_blue=255@c4_materials.wdl:2955; specular_red=44@c4_materials.wdl:2956; specular_green=44@c4_materials.wdl:2957
    // specular_blue=44@c4_materials.wdl:2958; emissive_red=12@c4_materials.wdl:2959; emissive_green=12@c4_materials.wdl:2960; emissive_blue=7@c4_materials.wdl:2961
    // power=0@c4_materials.wdl:2962; albedo=52@c4_materials.wdl:2963
    {"Mtl_bina_x", {}, 0x00000u, {227.0f, 212.0f, 212.0f}, {255.0f, 250.0f, 255.0f}, {44.0f, 44.0f, 44.0f}, {12.0f, 12.0f, 7.0f}, 0xfffu, 0.0f, 52.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=172@c4_materials.wdl:4802; ambient_green=172@c4_materials.wdl:4803; ambient_blue=172@c4_materials.wdl:4804; diffuse_red=248@c4_materials.wdl:4805
    // diffuse_green=239@c4_materials.wdl:4806; diffuse_blue=235@c4_materials.wdl:4807; specular_red=0@c4_materials.wdl:4808; specular_green=1@c4_materials.wdl:4809
    // specular_blue=1@c4_materials.wdl:4810; emissive_red=0@c4_materials.wdl:4811; emissive_green=0@c4_materials.wdl:4812; emissive_blue=0@c4_materials.wdl:4813
    // power=0@c4_materials.wdl:4814; albedo=50@c4_materials.wdl:4815
    {"Mtl_binalar_a", {}, 0x00000u, {172.0f, 172.0f, 172.0f}, {248.0f, 239.0f, 235.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=228@c4_materials.wdl:1421; ambient_green=228@c4_materials.wdl:1422; ambient_blue=228@c4_materials.wdl:1423; diffuse_red=255@c4_materials.wdl:1424
    // diffuse_green=255@c4_materials.wdl:1425; diffuse_blue=255@c4_materials.wdl:1426; specular_red=0@c4_materials.wdl:1427; specular_green=0@c4_materials.wdl:1428
    // specular_blue=0@c4_materials.wdl:1429; emissive_red=7@c4_materials.wdl:1430; emissive_green=10@c4_materials.wdl:1431; emissive_blue=12@c4_materials.wdl:1432
    // power=0@c4_materials.wdl:1433; albedo=-96@c4_materials.wdl:1434
    {"Mtl_c4_ayna_22", {}, 0x00000u, {228.0f, 228.0f, 228.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {7.0f, 10.0f, 12.0f}, 0xfffu, 0.0f, -96.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=32@c4_materials.wdl:5113; ambient_green=50@c4_materials.wdl:5114; ambient_blue=54@c4_materials.wdl:5115; diffuse_red=244@c4_materials.wdl:5116
    // diffuse_green=247@c4_materials.wdl:5117; diffuse_blue=253@c4_materials.wdl:5118; specular_red=103@c4_materials.wdl:5119; specular_green=105@c4_materials.wdl:5120
    // specular_blue=103@c4_materials.wdl:5121; emissive_red=77@c4_materials.wdl:5122; emissive_green=47@c4_materials.wdl:5123; emissive_blue=53@c4_materials.wdl:5124
    // power=0@c4_materials.wdl:5125; albedo=34@c4_materials.wdl:5126
    {"Mtl_c_ball", {}, 0x00000u, {32.0f, 50.0f, 54.0f}, {244.0f, 247.0f, 253.0f}, {103.0f, 105.0f, 103.0f}, {77.0f, 47.0f, 53.0f}, 0xfffu, 0.0f, 34.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=108@c4_materials.wdl:3214; ambient_green=119@c4_materials.wdl:3215; ambient_blue=132@c4_materials.wdl:3216; diffuse_red=112@c4_materials.wdl:3217
    // diffuse_green=117@c4_materials.wdl:3218; diffuse_blue=122@c4_materials.wdl:3219; specular_red=0@c4_materials.wdl:3220; specular_green=0@c4_materials.wdl:3221
    // specular_blue=0@c4_materials.wdl:3222; emissive_red=0@c4_materials.wdl:3223; emissive_green=0@c4_materials.wdl:3224; emissive_blue=0@c4_materials.wdl:3225
    // power=1@c4_materials.wdl:3226; albedo=45@c4_materials.wdl:3227
    {"Mtl_cadir", {}, 0x00000u, {108.0f, 119.0f, 132.0f}, {112.0f, 117.0f, 122.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 45.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:5575; ambient_green=145@c4_materials.wdl:5576; ambient_blue=145@c4_materials.wdl:5577; diffuse_red=183@c4_materials.wdl:5578
    // diffuse_green=180@c4_materials.wdl:5579; diffuse_blue=180@c4_materials.wdl:5580; specular_red=20@c4_materials.wdl:5581; specular_green=18@c4_materials.wdl:5582
    // specular_blue=18@c4_materials.wdl:5583; emissive_red=8@c4_materials.wdl:5584; emissive_green=8@c4_materials.wdl:5585; emissive_blue=8@c4_materials.wdl:5586
    // power=10@c4_materials.wdl:5587; albedo=41@c4_materials.wdl:5588
    {"Mtl_car3", {}, 0x00000u, {145.0f, 145.0f, 145.0f}, {183.0f, 180.0f, 180.0f}, {20.0f, 18.0f, 18.0f}, {8.0f, 8.0f, 8.0f}, 0xfffu, 10.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=170@c4_materials.wdl:3182; ambient_green=170@c4_materials.wdl:3183; ambient_blue=167@c4_materials.wdl:3184; diffuse_red=58@c4_materials.wdl:3185
    // diffuse_green=59@c4_materials.wdl:3186; diffuse_blue=59@c4_materials.wdl:3187; specular_red=238@c4_materials.wdl:3188; specular_green=236@c4_materials.wdl:3189
    // specular_blue=236@c4_materials.wdl:3190; emissive_red=48@c4_materials.wdl:3191; emissive_green=51@c4_materials.wdl:3192; emissive_blue=51@c4_materials.wdl:3193
    // power=3@c4_materials.wdl:3194; albedo=11@c4_materials.wdl:3195
    {"Mtl_celik_halat", {}, 0x00000u, {170.0f, 170.0f, 167.0f}, {58.0f, 59.0f, 59.0f}, {238.0f, 236.0f, 236.0f}, {48.0f, 51.0f, 51.0f}, 0xfffu, 3.0f, 11.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=93@c4_materials.wdl:2157; ambient_green=96@c4_materials.wdl:2158; ambient_blue=103@c4_materials.wdl:2159; diffuse_red=236@c4_materials.wdl:2160
    // diffuse_green=236@c4_materials.wdl:2161; diffuse_blue=255@c4_materials.wdl:2162; specular_red=0@c4_materials.wdl:2163; specular_green=0@c4_materials.wdl:2164
    // specular_blue=0@c4_materials.wdl:2165; emissive_red=0@c4_materials.wdl:2166; emissive_green=0@c4_materials.wdl:2167; emissive_blue=0@c4_materials.wdl:2168
    // power=0@c4_materials.wdl:2169; albedo=41@c4_materials.wdl:2170
    {"Mtl_cikis", {}, 0x00000u, {93.0f, 96.0f, 103.0f}, {236.0f, 236.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=161@c4_materials.wdl:2703; ambient_green=161@c4_materials.wdl:2704; ambient_blue=161@c4_materials.wdl:2705; diffuse_red=166@c4_materials.wdl:2706
    // diffuse_green=166@c4_materials.wdl:2707; diffuse_blue=166@c4_materials.wdl:2708; specular_red=0@c4_materials.wdl:2709; specular_green=0@c4_materials.wdl:2710
    // specular_blue=0@c4_materials.wdl:2711; emissive_red=13@c4_materials.wdl:2712; emissive_green=13@c4_materials.wdl:2713; emissive_blue=13@c4_materials.wdl:2714
    // power=0@c4_materials.wdl:2715; albedo=65@c4_materials.wdl:2716
    {"Mtl_cit", {}, 0x00000u, {161.0f, 161.0f, 161.0f}, {166.0f, 166.0f, 166.0f}, {0.0f, 0.0f, 0.0f}, {13.0f, 13.0f, 13.0f}, 0xfffu, 0.0f, 65.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4058; ambient_green=255@c4_materials.wdl:4059; ambient_blue=255@c4_materials.wdl:4060; diffuse_red=255@c4_materials.wdl:4061
    // diffuse_green=255@c4_materials.wdl:4062; diffuse_blue=255@c4_materials.wdl:4063; specular_red=0@c4_materials.wdl:4064; specular_green=0@c4_materials.wdl:4065
    // specular_blue=0@c4_materials.wdl:4066; emissive_red=0@c4_materials.wdl:4067; emissive_green=0@c4_materials.wdl:4068; emissive_blue=0@c4_materials.wdl:4069
    // power=0@c4_materials.wdl:4070; albedo=50@c4_materials.wdl:4071
    {"Mtl_cubuk_2", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=148@c4_materials.wdl:2669; ambient_green=148@c4_materials.wdl:2670; ambient_blue=148@c4_materials.wdl:2671; diffuse_red=255@c4_materials.wdl:2672
    // diffuse_green=255@c4_materials.wdl:2673; diffuse_blue=255@c4_materials.wdl:2674; specular_red=35@c4_materials.wdl:2675; specular_green=35@c4_materials.wdl:2676
    // specular_blue=35@c4_materials.wdl:2677; emissive_red=0@c4_materials.wdl:2678; emissive_green=0@c4_materials.wdl:2679; emissive_blue=0@c4_materials.wdl:2680
    // power=0@c4_materials.wdl:2681; albedo=22@c4_materials.wdl:2682
    {"Mtl_d2", {}, 0x00000u, {148.0f, 148.0f, 148.0f}, {255.0f, 255.0f, 255.0f}, {35.0f, 35.0f, 35.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 22.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"mtl_denizwater", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=178@c4_materials.wdl:2379; ambient_green=170@c4_materials.wdl:2380; ambient_blue=161@c4_materials.wdl:2381; diffuse_red=255@c4_materials.wdl:2382
    // diffuse_green=255@c4_materials.wdl:2383; diffuse_blue=255@c4_materials.wdl:2384; specular_red=9@c4_materials.wdl:2385; specular_green=4@c4_materials.wdl:2386
    // specular_blue=0@c4_materials.wdl:2387; emissive_red=0@c4_materials.wdl:2388; emissive_green=0@c4_materials.wdl:2389; emissive_blue=0@c4_materials.wdl:2390
    // power=0@c4_materials.wdl:2391; albedo=41@c4_materials.wdl:2392
    {"Mtl_depo", {}, 0x00000u, {178.0f, 170.0f, 161.0f}, {255.0f, 255.0f, 255.0f}, {9.0f, 4.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=120@c4_materials.wdl:2635; ambient_green=120@c4_materials.wdl:2636; ambient_blue=120@c4_materials.wdl:2637; diffuse_red=150@c4_materials.wdl:2638
    // diffuse_green=150@c4_materials.wdl:2639; diffuse_blue=150@c4_materials.wdl:2640; specular_red=47@c4_materials.wdl:2641; specular_green=47@c4_materials.wdl:2642
    // specular_blue=47@c4_materials.wdl:2643; emissive_red=0@c4_materials.wdl:2644; emissive_green=0@c4_materials.wdl:2645; emissive_blue=0@c4_materials.wdl:2646
    // power=0@c4_materials.wdl:2647; albedo=18@c4_materials.wdl:2648
    {"Mtl_dev_2", {}, 0x00000u, {120.0f, 120.0f, 120.0f}, {150.0f, 150.0f, 150.0f}, {47.0f, 47.0f, 47.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 18.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:2601; ambient_green=152@c4_materials.wdl:2602; ambient_blue=161@c4_materials.wdl:2603; diffuse_red=230@c4_materials.wdl:2604
    // diffuse_green=230@c4_materials.wdl:2605; diffuse_blue=230@c4_materials.wdl:2606; specular_red=7@c4_materials.wdl:2607; specular_green=7@c4_materials.wdl:2608
    // specular_blue=7@c4_materials.wdl:2609; emissive_red=0@c4_materials.wdl:2610; emissive_green=0@c4_materials.wdl:2611; emissive_blue=0@c4_materials.wdl:2612
    // power=0@c4_materials.wdl:2613; albedo=29@c4_materials.wdl:2614
    {"Mtl_dev_3", {}, 0x00000u, {140.0f, 152.0f, 161.0f}, {230.0f, 230.0f, 230.0f}, {7.0f, 7.0f, 7.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 29.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=114@c4_materials.wdl:2567; ambient_green=114@c4_materials.wdl:2568; ambient_blue=114@c4_materials.wdl:2569; diffuse_red=230@c4_materials.wdl:2570
    // diffuse_green=230@c4_materials.wdl:2571; diffuse_blue=230@c4_materials.wdl:2572; specular_red=7@c4_materials.wdl:2573; specular_green=7@c4_materials.wdl:2574
    // specular_blue=7@c4_materials.wdl:2575; emissive_red=0@c4_materials.wdl:2576; emissive_green=0@c4_materials.wdl:2577; emissive_blue=0@c4_materials.wdl:2578
    // power=0@c4_materials.wdl:2579; albedo=22@c4_materials.wdl:2580
    {"Mtl_dev_4", {}, 0x00000u, {114.0f, 114.0f, 114.0f}, {230.0f, 230.0f, 230.0f}, {7.0f, 7.0f, 7.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 22.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=251@c4_materials.wdl:1860; ambient_green=255@c4_materials.wdl:1861; ambient_blue=255@c4_materials.wdl:1862; diffuse_red=20@c4_materials.wdl:1863
    // diffuse_green=20@c4_materials.wdl:1864; diffuse_blue=20@c4_materials.wdl:1865; specular_red=0@c4_materials.wdl:1866; specular_green=0@c4_materials.wdl:1867
    // specular_blue=0@c4_materials.wdl:1868; emissive_red=30@c4_materials.wdl:1869; emissive_green=30@c4_materials.wdl:1870; emissive_blue=30@c4_materials.wdl:1871
    // power=0@c4_materials.wdl:1872; albedo=50@c4_materials.wdl:1873
    {"Mtl_direkler", {}, 0x00000u, {251.0f, 255.0f, 255.0f}, {20.0f, 20.0f, 20.0f}, {0.0f, 0.0f, 0.0f}, {30.0f, 30.0f, 30.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=164@c4_materials.wdl:5238; ambient_green=164@c4_materials.wdl:5239; ambient_blue=166@c4_materials.wdl:5240; diffuse_red=215@c4_materials.wdl:5241
    // diffuse_green=215@c4_materials.wdl:5242; diffuse_blue=219@c4_materials.wdl:5243; specular_red=0@c4_materials.wdl:5244; specular_green=0@c4_materials.wdl:5245
    // specular_blue=0@c4_materials.wdl:5246; emissive_red=0@c4_materials.wdl:5247; emissive_green=0@c4_materials.wdl:5248; emissive_blue=0@c4_materials.wdl:5249
    // power=0@c4_materials.wdl:5250; albedo=65@c4_materials.wdl:5251
    {"Mtl_don_2", {}, 0x00000u, {164.0f, 164.0f, 166.0f}, {215.0f, 215.0f, 219.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 65.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=153@c4_materials.wdl:1815; ambient_green=150@c4_materials.wdl:1816; ambient_blue=147@c4_materials.wdl:1817; diffuse_red=255@c4_materials.wdl:1818
    // diffuse_green=255@c4_materials.wdl:1819; diffuse_blue=255@c4_materials.wdl:1820; specular_red=32@c4_materials.wdl:1821; specular_green=33@c4_materials.wdl:1822
    // specular_blue=30@c4_materials.wdl:1823; emissive_red=0@c4_materials.wdl:1824; emissive_green=0@c4_materials.wdl:1825; emissive_blue=0@c4_materials.wdl:1826
    // power=0@c4_materials.wdl:1827; albedo=9@c4_materials.wdl:1828
    {"Mtl_don_duv", {}, 0x00000u, {153.0f, 150.0f, 147.0f}, {255.0f, 255.0f, 255.0f}, {32.0f, 33.0f, 30.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 9.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=183@c4_materials.wdl:5323; ambient_green=183@c4_materials.wdl:5324; ambient_blue=180@c4_materials.wdl:5325; diffuse_red=213@c4_materials.wdl:5326
    // diffuse_green=212@c4_materials.wdl:5327; diffuse_blue=212@c4_materials.wdl:5328; specular_red=0@c4_materials.wdl:5329; specular_green=0@c4_materials.wdl:5330
    // specular_blue=0@c4_materials.wdl:5331; emissive_red=15@c4_materials.wdl:5332; emissive_green=15@c4_materials.wdl:5333; emissive_blue=15@c4_materials.wdl:5334
    // power=0@c4_materials.wdl:5335; albedo=50@c4_materials.wdl:5336
    {"Mtl_don_ok", {}, 0x00000u, {183.0f, 183.0f, 180.0f}, {213.0f, 212.0f, 212.0f}, {0.0f, 0.0f, 0.0f}, {15.0f, 15.0f, 15.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=143@c4_materials.wdl:1830; ambient_green=140@c4_materials.wdl:1831; ambient_blue=137@c4_materials.wdl:1832; diffuse_red=255@c4_materials.wdl:1833
    // diffuse_green=255@c4_materials.wdl:1834; diffuse_blue=255@c4_materials.wdl:1835; specular_red=42@c4_materials.wdl:1836; specular_green=43@c4_materials.wdl:1837
    // specular_blue=40@c4_materials.wdl:1838; emissive_red=0@c4_materials.wdl:1839; emissive_green=0@c4_materials.wdl:1840; emissive_blue=0@c4_materials.wdl:1841
    // power=0@c4_materials.wdl:1842; albedo=9@c4_materials.wdl:1843
    {"Mtl_don_tel", {}, 0x00000u, {143.0f, 140.0f, 137.0f}, {255.0f, 255.0f, 255.0f}, {42.0f, 43.0f, 40.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 9.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=169@c4_materials.wdl:5787; ambient_green=169@c4_materials.wdl:5788; ambient_blue=169@c4_materials.wdl:5789; diffuse_red=196@c4_materials.wdl:5790
    // diffuse_green=198@c4_materials.wdl:5791; diffuse_blue=195@c4_materials.wdl:5792; specular_red=18@c4_materials.wdl:5793; specular_green=18@c4_materials.wdl:5794
    // specular_blue=18@c4_materials.wdl:5795; emissive_red=21@c4_materials.wdl:5796; emissive_green=21@c4_materials.wdl:5797; emissive_blue=21@c4_materials.wdl:5798
    // power=0@c4_materials.wdl:5799; albedo=70@c4_materials.wdl:5800
    {"Mtl_drek", {}, 0x00000u, {169.0f, 169.0f, 169.0f}, {196.0f, 198.0f, 195.0f}, {18.0f, 18.0f, 18.0f}, {21.0f, 21.0f, 21.0f}, 0xfffu, 0.0f, 70.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=170@c4_materials.wdl:5931; ambient_green=170@c4_materials.wdl:5932; ambient_blue=170@c4_materials.wdl:5933; diffuse_red=60@c4_materials.wdl:5934
    // diffuse_green=60@c4_materials.wdl:5935; diffuse_blue=60@c4_materials.wdl:5936; specular_red=30@c4_materials.wdl:5937; specular_green=30@c4_materials.wdl:5938
    // specular_blue=27@c4_materials.wdl:5939; emissive_red=0@c4_materials.wdl:5940; emissive_green=0@c4_materials.wdl:5941; emissive_blue=0@c4_materials.wdl:5942
    // power=2@c4_materials.wdl:5943; albedo=137@c4_materials.wdl:5944
    {"Mtl_dur_levha", {}, 0x00000u, {170.0f, 170.0f, 170.0f}, {60.0f, 60.0f, 60.0f}, {30.0f, 30.0f, 27.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 2.0f, 137.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=124@c4_materials.wdl:3167; ambient_green=130@c4_materials.wdl:3168; ambient_blue=132@c4_materials.wdl:3169; diffuse_red=207@c4_materials.wdl:3170
    // diffuse_green=210@c4_materials.wdl:3171; diffuse_blue=213@c4_materials.wdl:3172; specular_red=0@c4_materials.wdl:3173; specular_green=0@c4_materials.wdl:3174
    // specular_blue=3@c4_materials.wdl:3175; emissive_red=4@c4_materials.wdl:3176; emissive_green=6@c4_materials.wdl:3177; emissive_blue=7@c4_materials.wdl:3178
    // power=3@c4_materials.wdl:3179; albedo=23@c4_materials.wdl:3180
    {"Mtl_durak", {}, 0x00000u, {124.0f, 130.0f, 132.0f}, {207.0f, 210.0f, 213.0f}, {0.0f, 0.0f, 3.0f}, {4.0f, 6.0f, 7.0f}, 0xfffu, 3.0f, 23.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4768; ambient_green=255@c4_materials.wdl:4769; ambient_blue=255@c4_materials.wdl:4770; diffuse_red=255@c4_materials.wdl:4771
    // diffuse_green=255@c4_materials.wdl:4772; diffuse_blue=255@c4_materials.wdl:4773; specular_red=16@c4_materials.wdl:4774; specular_green=16@c4_materials.wdl:4775
    // specular_blue=16@c4_materials.wdl:4776; emissive_red=0@c4_materials.wdl:4777; emissive_green=0@c4_materials.wdl:4778; emissive_blue=0@c4_materials.wdl:4779
    // power=0@c4_materials.wdl:4780; albedo=50@c4_materials.wdl:4781
    {"Mtl_duvar_3", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {16.0f, 16.0f, 16.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=222@c4_materials.wdl:6032; ambient_green=222@c4_materials.wdl:6033; ambient_blue=222@c4_materials.wdl:6034; diffuse_red=125@c4_materials.wdl:6035
    // diffuse_green=126@c4_materials.wdl:6036; diffuse_blue=126@c4_materials.wdl:6037; specular_red=0@c4_materials.wdl:6038; specular_green=0@c4_materials.wdl:6039
    // specular_blue=0@c4_materials.wdl:6040; emissive_red=0@c4_materials.wdl:6041; emissive_green=0@c4_materials.wdl:6042; emissive_blue=0@c4_materials.wdl:6043
    // power=0@c4_materials.wdl:6044; albedo=50@c4_materials.wdl:6045
    {"Mtl_duvar_a", {}, 0x00000u, {222.0f, 222.0f, 222.0f}, {125.0f, 126.0f, 126.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4024; ambient_green=255@c4_materials.wdl:4025; ambient_blue=255@c4_materials.wdl:4026; diffuse_red=255@c4_materials.wdl:4027
    // diffuse_green=255@c4_materials.wdl:4028; diffuse_blue=255@c4_materials.wdl:4029; specular_red=16@c4_materials.wdl:4030; specular_green=16@c4_materials.wdl:4031
    // specular_blue=16@c4_materials.wdl:4032; emissive_red=0@c4_materials.wdl:4033; emissive_green=0@c4_materials.wdl:4034; emissive_blue=0@c4_materials.wdl:4035
    // power=0@c4_materials.wdl:4036; albedo=50@c4_materials.wdl:4037
    {"Mtl_duvar_ek", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {16.0f, 16.0f, 16.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=187@c4_materials.wdl:3067; ambient_green=187@c4_materials.wdl:3068; ambient_blue=186@c4_materials.wdl:3069; diffuse_red=80@c4_materials.wdl:3070
    // diffuse_green=80@c4_materials.wdl:3071; diffuse_blue=83@c4_materials.wdl:3072; specular_red=0@c4_materials.wdl:3073; specular_green=0@c4_materials.wdl:3074
    // specular_blue=0@c4_materials.wdl:3075; emissive_red=0@c4_materials.wdl:3076; emissive_green=0@c4_materials.wdl:3077; emissive_blue=0@c4_materials.wdl:3078
    // power=0@c4_materials.wdl:3079; albedo=50@c4_materials.wdl:3080
    {"Mtl_duvar_wan", {}, 0x00000u, {187.0f, 187.0f, 186.0f}, {80.0f, 80.0f, 83.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=143@c4_materials.wdl:4919; ambient_green=155@c4_materials.wdl:4920; ambient_blue=155@c4_materials.wdl:4921; diffuse_red=138@c4_materials.wdl:4922
    // diffuse_green=163@c4_materials.wdl:4923; diffuse_blue=166@c4_materials.wdl:4924; specular_red=0@c4_materials.wdl:4925; specular_green=0@c4_materials.wdl:4926
    // specular_blue=0@c4_materials.wdl:4927; emissive_red=0@c4_materials.wdl:4928; emissive_green=0@c4_materials.wdl:4929; emissive_blue=0@c4_materials.wdl:4930
    // power=0@c4_materials.wdl:4931; albedo=50@c4_materials.wdl:4932
    {"Mtl_duvset1", {}, 0x00000u, {143.0f, 155.0f, 155.0f}, {138.0f, 163.0f, 166.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=143@c4_materials.wdl:4953; ambient_green=155@c4_materials.wdl:4954; ambient_blue=155@c4_materials.wdl:4955; diffuse_red=138@c4_materials.wdl:4956
    // diffuse_green=163@c4_materials.wdl:4957; diffuse_blue=166@c4_materials.wdl:4958; specular_red=0@c4_materials.wdl:4959; specular_green=0@c4_materials.wdl:4960
    // specular_blue=0@c4_materials.wdl:4961; emissive_red=0@c4_materials.wdl:4962; emissive_green=0@c4_materials.wdl:4963; emissive_blue=0@c4_materials.wdl:4964
    // power=0@c4_materials.wdl:4965; albedo=50@c4_materials.wdl:4966
    {"Mtl_duvset2", {}, 0x00000u, {143.0f, 155.0f, 155.0f}, {138.0f, 163.0f, 166.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=174@c4_materials.wdl:4530; ambient_green=178@c4_materials.wdl:4531; ambient_blue=178@c4_materials.wdl:4532; diffuse_red=255@c4_materials.wdl:4533
    // diffuse_green=255@c4_materials.wdl:4534; diffuse_blue=255@c4_materials.wdl:4535; specular_red=15@c4_materials.wdl:4536; specular_green=15@c4_materials.wdl:4537
    // specular_blue=15@c4_materials.wdl:4538; emissive_red=0@c4_materials.wdl:4539; emissive_green=0@c4_materials.wdl:4540; emissive_blue=0@c4_materials.wdl:4541
    // power=0@c4_materials.wdl:4542; albedo=50@c4_materials.wdl:4543
    {"Mtl_DVR", {}, 0x00000u, {174.0f, 178.0f, 178.0f}, {255.0f, 255.0f, 255.0f}, {15.0f, 15.0f, 15.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=116@c4_materials.wdl:1768; ambient_green=111@c4_materials.wdl:1769; ambient_blue=109@c4_materials.wdl:1770; diffuse_red=196@c4_materials.wdl:1771
    // diffuse_green=193@c4_materials.wdl:1772; diffuse_blue=193@c4_materials.wdl:1773; specular_red=0@c4_materials.wdl:1774; specular_green=0@c4_materials.wdl:1775
    // specular_blue=0@c4_materials.wdl:1776; emissive_red=30@c4_materials.wdl:1777; emissive_green=30@c4_materials.wdl:1778; emissive_blue=30@c4_materials.wdl:1779
    // power=0@c4_materials.wdl:1780; albedo=93@c4_materials.wdl:1781
    {"Mtl_engel_2", {}, 0x00000u, {116.0f, 111.0f, 109.0f}, {196.0f, 193.0f, 193.0f}, {0.0f, 0.0f, 0.0f}, {30.0f, 30.0f, 30.0f}, 0xfffu, 0.0f, 93.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No recovered material definition; all scalar fields unknown.
    {"Mtl_ENGEL_ORTA", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=172@c4_materials.wdl:4836; ambient_green=172@c4_materials.wdl:4837; ambient_blue=172@c4_materials.wdl:4838; diffuse_red=248@c4_materials.wdl:4839
    // diffuse_green=239@c4_materials.wdl:4840; diffuse_blue=235@c4_materials.wdl:4841; specular_red=0@c4_materials.wdl:4842; specular_green=1@c4_materials.wdl:4843
    // specular_blue=1@c4_materials.wdl:4844; emissive_red=0@c4_materials.wdl:4845; emissive_green=0@c4_materials.wdl:4846; emissive_blue=0@c4_materials.wdl:4847
    // power=0@c4_materials.wdl:4848; albedo=50@c4_materials.wdl:4849
    {"Mtl_ev_2", {}, 0x00000u, {172.0f, 172.0f, 172.0f}, {248.0f, 239.0f, 235.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=109@c4_materials.wdl:3050; ambient_green=109@c4_materials.wdl:3051; ambient_blue=109@c4_materials.wdl:3052; diffuse_red=255@c4_materials.wdl:3053
    // diffuse_green=255@c4_materials.wdl:3054; diffuse_blue=255@c4_materials.wdl:3055; specular_red=0@c4_materials.wdl:3056; specular_green=0@c4_materials.wdl:3057
    // specular_blue=0@c4_materials.wdl:3058; emissive_red=0@c4_materials.wdl:3059; emissive_green=0@c4_materials.wdl:3060; emissive_blue=0@c4_materials.wdl:3061
    // power=0@c4_materials.wdl:3062; albedo=41@c4_materials.wdl:3063
    {"Mtl_EV_X", {}, 0x00000u, {109.0f, 109.0f, 109.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=169@c4_materials.wdl:2205; ambient_green=163@c4_materials.wdl:2206; ambient_blue=163@c4_materials.wdl:2207; diffuse_red=204@c4_materials.wdl:2208
    // diffuse_green=204@c4_materials.wdl:2209; diffuse_blue=204@c4_materials.wdl:2210; specular_red=0@c4_materials.wdl:2211; specular_green=0@c4_materials.wdl:2212
    // specular_blue=0@c4_materials.wdl:2213; emissive_red=0@c4_materials.wdl:2214; emissive_green=0@c4_materials.wdl:2215; emissive_blue=0@c4_materials.wdl:2216
    // power=0@c4_materials.wdl:2217; albedo=50@c4_materials.wdl:2218
    {"Mtl_ex_yol_kald", {}, 0x00000u, {169.0f, 163.0f, 163.0f}, {204.0f, 204.0f, 204.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_exyol_tun_kal", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=160@c4_materials.wdl:6888; ambient_green=160@c4_materials.wdl:6889; ambient_blue=160@c4_materials.wdl:6890; diffuse_red=255@c4_materials.wdl:6891
    // diffuse_green=255@c4_materials.wdl:6892; diffuse_blue=255@c4_materials.wdl:6893; specular_red=0@c4_materials.wdl:6894; specular_green=0@c4_materials.wdl:6895
    // specular_blue=0@c4_materials.wdl:6896; emissive_red=0@c4_materials.wdl:6897; emissive_green=0@c4_materials.wdl:6898; emissive_blue=0@c4_materials.wdl:6899
    // power=0@c4_materials.wdl:6900; albedo=50@c4_materials.wdl:6901
    {"Mtl_exyol_tunel", {}, 0x00000u, {160.0f, 160.0f, 160.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=210@c4_materials.wdl:2750; ambient_green=190@c4_materials.wdl:2751; ambient_blue=163@c4_materials.wdl:2752; diffuse_red=0@c4_materials.wdl:2753
    // diffuse_green=0@c4_materials.wdl:2754; diffuse_blue=0@c4_materials.wdl:2755; specular_red=0@c4_materials.wdl:2756; specular_green=0@c4_materials.wdl:2757
    // specular_blue=0@c4_materials.wdl:2758; emissive_red=83@c4_materials.wdl:2759; emissive_green=38@c4_materials.wdl:2760; emissive_blue=12@c4_materials.wdl:2761
    // power=0@c4_materials.wdl:2762; albedo=-100@c4_materials.wdl:2763
    {"Mtl_foto", {}, 0x00000u, {210.0f, 190.0f, 163.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {83.0f, 38.0f, 12.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=148@c4_materials.wdl:2984; ambient_green=172@c4_materials.wdl:2985; ambient_blue=172@c4_materials.wdl:2986; diffuse_red=255@c4_materials.wdl:2987
    // diffuse_green=255@c4_materials.wdl:2988; diffuse_blue=255@c4_materials.wdl:2989; specular_red=175@c4_materials.wdl:2990; specular_green=91@c4_materials.wdl:2991
    // specular_blue=0@c4_materials.wdl:2992; emissive_red=9@c4_materials.wdl:2993; emissive_green=9@c4_materials.wdl:2994; emissive_blue=9@c4_materials.wdl:2995
    // power=0@c4_materials.wdl:2996; albedo=36@c4_materials.wdl:2997
    {"Mtl_g2", {}, 0x00000u, {148.0f, 172.0f, 172.0f}, {255.0f, 255.0f, 255.0f}, {175.0f, 91.0f, 0.0f}, {9.0f, 9.0f, 9.0f}, 0xfffu, 0.0f, 36.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=195@c4_materials.wdl:3018; ambient_green=195@c4_materials.wdl:3019; ambient_blue=196@c4_materials.wdl:3020; diffuse_red=255@c4_materials.wdl:3021
    // diffuse_green=255@c4_materials.wdl:3022; diffuse_blue=255@c4_materials.wdl:3023; specular_red=141@c4_materials.wdl:3024; specular_green=148@c4_materials.wdl:3025
    // specular_blue=149@c4_materials.wdl:3026; emissive_red=19@c4_materials.wdl:3027; emissive_green=19@c4_materials.wdl:3028; emissive_blue=19@c4_materials.wdl:3029
    // power=1@c4_materials.wdl:3030; albedo=-3@c4_materials.wdl:3031
    {"Mtl_gemi", {}, 0x00000u, {195.0f, 195.0f, 196.0f}, {255.0f, 255.0f, 255.0f}, {141.0f, 148.0f, 149.0f}, {19.0f, 19.0f, 19.0f}, 0xfffu, 1.0f, -3.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_gg", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_gg2", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=0@c4_materials.wdl:784; ambient_green=29@c4_materials.wdl:785; ambient_blue=30@c4_materials.wdl:786; diffuse_red=241@c4_materials.wdl:787
    // diffuse_green=244@c4_materials.wdl:788; diffuse_blue=241@c4_materials.wdl:789; specular_red=0@c4_materials.wdl:790; specular_green=0@c4_materials.wdl:791
    // specular_blue=0@c4_materials.wdl:792; emissive_red=36@c4_materials.wdl:793; emissive_green=30@c4_materials.wdl:794; emissive_blue=22@c4_materials.wdl:795
    // power=0@c4_materials.wdl:796; albedo=18@c4_materials.wdl:797
    {"Mtl_ghost", {}, 0x00000u, {0.0f, 29.0f, 30.0f}, {241.0f, 244.0f, 241.0f}, {0.0f, 0.0f, 0.0f}, {36.0f, 30.0f, 22.0f}, 0xfffu, 0.0f, 18.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=228@c4_materials.wdl:5949; ambient_green=228@c4_materials.wdl:5950; ambient_blue=228@c4_materials.wdl:5951; diffuse_red=255@c4_materials.wdl:5952
    // diffuse_green=255@c4_materials.wdl:5953; diffuse_blue=255@c4_materials.wdl:5954; specular_red=0@c4_materials.wdl:5955; specular_green=0@c4_materials.wdl:5956
    // specular_blue=0@c4_materials.wdl:5957; emissive_red=0@c4_materials.wdl:5958; emissive_green=0@c4_materials.wdl:5959; emissive_blue=0@c4_materials.wdl:5960
    // power=0@c4_materials.wdl:5961; albedo=50@c4_materials.wdl:5962
    {"Mtl_gise_cati", {}, 0x00000u, {228.0f, 228.0f, 228.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=117@c4_materials.wdl:3960; ambient_green=117@c4_materials.wdl:3961; ambient_blue=117@c4_materials.wdl:3962; diffuse_red=196@c4_materials.wdl:3963
    // diffuse_green=199@c4_materials.wdl:3964; diffuse_blue=204@c4_materials.wdl:3965; specular_red=0@c4_materials.wdl:3966; specular_green=0@c4_materials.wdl:3967
    // specular_blue=0@c4_materials.wdl:3968; emissive_red=0@c4_materials.wdl:3969; emissive_green=0@c4_materials.wdl:3970; emissive_blue=0@c4_materials.wdl:3971
    // power=0@c4_materials.wdl:3972; albedo=27@c4_materials.wdl:3973
    {"Mtl_gise_koru", {}, 0x00000u, {117.0f, 117.0f, 117.0f}, {196.0f, 199.0f, 204.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 27.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:5870; ambient_green=135@c4_materials.wdl:5871; ambient_blue=135@c4_materials.wdl:5872; diffuse_red=53@c4_materials.wdl:5873
    // diffuse_green=53@c4_materials.wdl:5874; diffuse_blue=53@c4_materials.wdl:5875; specular_red=59@c4_materials.wdl:5876; specular_green=59@c4_materials.wdl:5877
    // specular_blue=59@c4_materials.wdl:5878; emissive_red=0@c4_materials.wdl:5879; emissive_green=0@c4_materials.wdl:5880; emissive_blue=0@c4_materials.wdl:5881
    // power=0@c4_materials.wdl:5882; albedo=50@c4_materials.wdl:5883
    {"Mtl_gise_reklam", {}, 0x00000u, {145.0f, 135.0f, 135.0f}, {53.0f, 53.0f, 53.0f}, {59.0f, 59.0f, 59.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:633; ambient_green=253@c4_materials.wdl:634; ambient_blue=247@c4_materials.wdl:635; diffuse_red=255@c4_materials.wdl:636
    // diffuse_green=255@c4_materials.wdl:637; diffuse_blue=255@c4_materials.wdl:638; specular_red=0@c4_materials.wdl:639; specular_green=0@c4_materials.wdl:640
    // specular_blue=1@c4_materials.wdl:641; emissive_red=254@c4_materials.wdl:642; emissive_green=254@c4_materials.wdl:643; emissive_blue=255@c4_materials.wdl:644
    // power=0@c4_materials.wdl:645; albedo=-100@c4_materials.wdl:646
    {"Mtl_glow", {}, 0x00000u, {255.0f, 253.0f, 247.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 1.0f}, {254.0f, 254.0f, 255.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=126@c4_materials.wdl:954; ambient_green=146@c4_materials.wdl:955; ambient_blue=146@c4_materials.wdl:956; diffuse_red=245@c4_materials.wdl:957
    // diffuse_green=21@c4_materials.wdl:958; diffuse_blue=181@c4_materials.wdl:959; specular_red=143@c4_materials.wdl:960; specular_green=146@c4_materials.wdl:961
    // specular_blue=149@c4_materials.wdl:962; emissive_red=10@c4_materials.wdl:963; emissive_green=16@c4_materials.wdl:964; emissive_blue=25@c4_materials.wdl:965
    // power=3@c4_materials.wdl:966; albedo=9@c4_materials.wdl:967
    {"Mtl_hamsi_gumus", {}, 0x00000u, {126.0f, 146.0f, 146.0f}, {245.0f, 21.0f, 181.0f}, {143.0f, 146.0f, 149.0f}, {10.0f, 16.0f, 25.0f}, 0xfffu, 3.0f, 9.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=137@c4_materials.wdl:939; ambient_green=79@c4_materials.wdl:940; ambient_blue=36@c4_materials.wdl:941; diffuse_red=255@c4_materials.wdl:942
    // diffuse_green=255@c4_materials.wdl:943; diffuse_blue=255@c4_materials.wdl:944; specular_red=4@c4_materials.wdl:945; specular_green=4@c4_materials.wdl:946
    // specular_blue=9@c4_materials.wdl:947; emissive_red=45@c4_materials.wdl:948; emissive_green=22@c4_materials.wdl:949; emissive_blue=13@c4_materials.wdl:950
    // power=0@c4_materials.wdl:951; albedo=-73@c4_materials.wdl:952
    {"Mtl_hamsi_red", {}, 0x00000u, {137.0f, 79.0f, 36.0f}, {255.0f, 255.0f, 255.0f}, {4.0f, 4.0f, 9.0f}, {45.0f, 22.0f, 13.0f}, 0xfffu, 0.0f, -73.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=129@c4_materials.wdl:924; ambient_green=134@c4_materials.wdl:925; ambient_blue=116@c4_materials.wdl:926; diffuse_red=255@c4_materials.wdl:927
    // diffuse_green=255@c4_materials.wdl:928; diffuse_blue=255@c4_materials.wdl:929; specular_red=53@c4_materials.wdl:930; specular_green=54@c4_materials.wdl:931
    // specular_blue=54@c4_materials.wdl:932; emissive_red=18@c4_materials.wdl:933; emissive_green=18@c4_materials.wdl:934; emissive_blue=18@c4_materials.wdl:935
    // power=0@c4_materials.wdl:936; albedo=-73@c4_materials.wdl:937
    {"Mtl_hamsi_sari", {}, 0x00000u, {129.0f, 134.0f, 116.0f}, {255.0f, 255.0f, 255.0f}, {53.0f, 54.0f, 54.0f}, {18.0f, 18.0f, 18.0f}, 0xfffu, 0.0f, -73.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=97@c4_materials.wdl:969; ambient_green=88@c4_materials.wdl:970; ambient_blue=38@c4_materials.wdl:971; diffuse_red=245@c4_materials.wdl:972
    // diffuse_green=19@c4_materials.wdl:973; diffuse_blue=122@c4_materials.wdl:974; specular_red=244@c4_materials.wdl:975; specular_green=222@c4_materials.wdl:976
    // specular_blue=198@c4_materials.wdl:977; emissive_red=19@c4_materials.wdl:978; emissive_green=18@c4_materials.wdl:979; emissive_blue=27@c4_materials.wdl:980
    // power=0@c4_materials.wdl:981; albedo=4@c4_materials.wdl:982
    {"Mtl_hamsi_tur", {}, 0x00000u, {97.0f, 88.0f, 38.0f}, {245.0f, 19.0f, 122.0f}, {244.0f, 222.0f, 198.0f}, {19.0f, 18.0f, 27.0f}, 0xfffu, 0.0f, 4.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:2445; ambient_green=230@c4_materials.wdl:2446; ambient_blue=215@c4_materials.wdl:2447; diffuse_red=255@c4_materials.wdl:2448
    // diffuse_green=232@c4_materials.wdl:2449; diffuse_blue=181@c4_materials.wdl:2450; specular_red=0@c4_materials.wdl:2451; specular_green=0@c4_materials.wdl:2452
    // specular_blue=0@c4_materials.wdl:2453; emissive_red=27@c4_materials.wdl:2454; emissive_green=21@c4_materials.wdl:2455; emissive_blue=0@c4_materials.wdl:2456
    // power=0@c4_materials.wdl:2457; albedo=88@c4_materials.wdl:2458
    {"Mtl_hell", {}, 0x00000u, {255.0f, 230.0f, 215.0f}, {255.0f, 232.0f, 181.0f}, {0.0f, 0.0f, 0.0f}, {27.0f, 21.0f, 0.0f}, 0xfffu, 0.0f, 88.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=206@c4_materials.wdl:1492; ambient_green=218@c4_materials.wdl:1493; ambient_blue=213@c4_materials.wdl:1494; diffuse_red=59@c4_materials.wdl:1495
    // diffuse_green=59@c4_materials.wdl:1496; diffuse_blue=59@c4_materials.wdl:1497; specular_red=0@c4_materials.wdl:1498; specular_green=0@c4_materials.wdl:1499
    // specular_blue=0@c4_materials.wdl:1500; emissive_red=24@c4_materials.wdl:1501; emissive_green=21@c4_materials.wdl:1502; emissive_blue=15@c4_materials.wdl:1503
    // power=0@c4_materials.wdl:1504; albedo=50@c4_materials.wdl:1505
    {"Mtl_ic_cam", {}, 0x00000u, {206.0f, 218.0f, 213.0f}, {59.0f, 59.0f, 59.0f}, {0.0f, 0.0f, 0.0f}, {24.0f, 21.0f, 15.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=206@c4_materials.wdl:1509; ambient_green=218@c4_materials.wdl:1510; ambient_blue=213@c4_materials.wdl:1511; diffuse_red=59@c4_materials.wdl:1512
    // diffuse_green=59@c4_materials.wdl:1513; diffuse_blue=59@c4_materials.wdl:1514; specular_red=0@c4_materials.wdl:1515; specular_green=0@c4_materials.wdl:1516
    // specular_blue=0@c4_materials.wdl:1517; emissive_red=24@c4_materials.wdl:1518; emissive_green=21@c4_materials.wdl:1519; emissive_blue=15@c4_materials.wdl:1520
    // power=0@c4_materials.wdl:1521; albedo=50@c4_materials.wdl:1522
    {"Mtl_ic_panel", {}, 0x00000u, {206.0f, 218.0f, 213.0f}, {59.0f, 59.0f, 59.0f}, {0.0f, 0.0f, 0.0f}, {24.0f, 21.0f, 15.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=184@c4_materials.wdl:1460; ambient_green=195@c4_materials.wdl:1461; ambient_blue=186@c4_materials.wdl:1462; diffuse_red=134@c4_materials.wdl:1463
    // diffuse_green=134@c4_materials.wdl:1464; diffuse_blue=137@c4_materials.wdl:1465; specular_red=1@c4_materials.wdl:1466; specular_green=0@c4_materials.wdl:1467
    // specular_blue=0@c4_materials.wdl:1468; emissive_red=0@c4_materials.wdl:1469; emissive_green=0@c4_materials.wdl:1470; emissive_blue=0@c4_materials.wdl:1471
    // power=0@c4_materials.wdl:1472; albedo=50@c4_materials.wdl:1473
    {"Mtl_ic_simit", {}, 0x00000u, {184.0f, 195.0f, 186.0f}, {134.0f, 134.0f, 137.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=206@c4_materials.wdl:1550; ambient_green=218@c4_materials.wdl:1551; ambient_blue=213@c4_materials.wdl:1552; diffuse_red=59@c4_materials.wdl:1553
    // diffuse_green=59@c4_materials.wdl:1554; diffuse_blue=59@c4_materials.wdl:1555; specular_red=0@c4_materials.wdl:1556; specular_green=0@c4_materials.wdl:1557
    // specular_blue=0@c4_materials.wdl:1558; emissive_red=24@c4_materials.wdl:1559; emissive_green=21@c4_materials.wdl:1560; emissive_blue=15@c4_materials.wdl:1561
    // power=0@c4_materials.wdl:1562; albedo=50@c4_materials.wdl:1563
    {"Mtl_ic_tavan", {}, 0x00000u, {206.0f, 218.0f, 213.0f}, {59.0f, 59.0f, 59.0f}, {0.0f, 0.0f, 0.0f}, {24.0f, 21.0f, 15.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=102@c4_materials.wdl:6415; ambient_green=102@c4_materials.wdl:6416; ambient_blue=102@c4_materials.wdl:6417; diffuse_red=99@c4_materials.wdl:6418
    // diffuse_green=99@c4_materials.wdl:6419; diffuse_blue=99@c4_materials.wdl:6420; specular_red=22@c4_materials.wdl:6421; specular_green=22@c4_materials.wdl:6422
    // specular_blue=22@c4_materials.wdl:6423; emissive_red=33@c4_materials.wdl:6424; emissive_green=33@c4_materials.wdl:6425; emissive_blue=33@c4_materials.wdl:6426
    // power=0@c4_materials.wdl:6427; albedo=200@c4_materials.wdl:6428
    {"Mtl_ins_kork", {}, 0x00000u, {102.0f, 102.0f, 102.0f}, {99.0f, 99.0f, 99.0f}, {22.0f, 22.0f, 22.0f}, {33.0f, 33.0f, 33.0f}, 0xfffu, 0.0f, 200.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=143@c4_materials.wdl:1734; ambient_green=143@c4_materials.wdl:1735; ambient_blue=140@c4_materials.wdl:1736; diffuse_red=178@c4_materials.wdl:1737
    // diffuse_green=175@c4_materials.wdl:1738; diffuse_blue=175@c4_materials.wdl:1739; specular_red=45@c4_materials.wdl:1740; specular_green=42@c4_materials.wdl:1741
    // specular_blue=29@c4_materials.wdl:1742; emissive_red=0@c4_materials.wdl:1743; emissive_green=0@c4_materials.wdl:1744; emissive_blue=0@c4_materials.wdl:1745
    // power=0@c4_materials.wdl:1746; albedo=43@c4_materials.wdl:1747
    {"Mtl_insaat_bez", {}, 0x00000u, {143.0f, 143.0f, 140.0f}, {178.0f, 175.0f, 175.0f}, {45.0f, 42.0f, 29.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 43.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=233@c4_materials.wdl:1687; ambient_green=206@c4_materials.wdl:1688; ambient_blue=216@c4_materials.wdl:1689; diffuse_red=255@c4_materials.wdl:1690
    // diffuse_green=255@c4_materials.wdl:1691; diffuse_blue=255@c4_materials.wdl:1692; specular_red=132@c4_materials.wdl:1693; specular_green=119@c4_materials.wdl:1694
    // specular_blue=108@c4_materials.wdl:1695; emissive_red=0@c4_materials.wdl:1696; emissive_green=0@c4_materials.wdl:1697; emissive_blue=0@c4_materials.wdl:1698
    // power=0@c4_materials.wdl:1699; albedo=-42@c4_materials.wdl:1700
    {"Mtl_iskeke_mar", {}, 0x00000u, {233.0f, 206.0f, 216.0f}, {255.0f, 255.0f, 255.0f}, {132.0f, 119.0f, 108.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, -42.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=141@c4_materials.wdl:1717; ambient_green=148@c4_materials.wdl:1718; ambient_blue=148@c4_materials.wdl:1719; diffuse_red=250@c4_materials.wdl:1720
    // diffuse_green=244@c4_materials.wdl:1721; diffuse_blue=244@c4_materials.wdl:1722; specular_red=0@c4_materials.wdl:1723; specular_green=0@c4_materials.wdl:1724
    // specular_blue=0@c4_materials.wdl:1725; emissive_red=91@c4_materials.wdl:1726; emissive_green=102@c4_materials.wdl:1727; emissive_blue=114@c4_materials.wdl:1728
    // power=0@c4_materials.wdl:1729; albedo=-91@c4_materials.wdl:1730
    {"Mtl_iskele_kald", {}, 0x00000u, {141.0f, 148.0f, 148.0f}, {250.0f, 244.0f, 244.0f}, {0.0f, 0.0f, 0.0f}, {91.0f, 102.0f, 114.0f}, 0xfffu, 0.0f, -91.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:1702; ambient_green=220@c4_materials.wdl:1703; ambient_blue=220@c4_materials.wdl:1704; diffuse_red=250@c4_materials.wdl:1705
    // diffuse_green=244@c4_materials.wdl:1706; diffuse_blue=244@c4_materials.wdl:1707; specular_red=0@c4_materials.wdl:1708; specular_green=0@c4_materials.wdl:1709
    // specular_blue=0@c4_materials.wdl:1710; emissive_red=149@c4_materials.wdl:1711; emissive_green=149@c4_materials.wdl:1712; emissive_blue=125@c4_materials.wdl:1713
    // power=0@c4_materials.wdl:1714; albedo=-91@c4_materials.wdl:1715
    {"Mtl_iskele_kork", {}, 0x00000u, {230.0f, 220.0f, 220.0f}, {250.0f, 244.0f, 244.0f}, {0.0f, 0.0f, 0.0f}, {149.0f, 149.0f, 125.0f}, 0xfffu, 0.0f, -91.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=198@c4_materials.wdl:4462; ambient_green=199@c4_materials.wdl:4463; ambient_blue=199@c4_materials.wdl:4464; diffuse_red=255@c4_materials.wdl:4465
    // diffuse_green=255@c4_materials.wdl:4466; diffuse_blue=255@c4_materials.wdl:4467; specular_red=0@c4_materials.wdl:4468; specular_green=0@c4_materials.wdl:4469
    // specular_blue=0@c4_materials.wdl:4470; emissive_red=0@c4_materials.wdl:4471; emissive_green=0@c4_materials.wdl:4472; emissive_blue=0@c4_materials.wdl:4473
    // power=0@c4_materials.wdl:4474; albedo=29@c4_materials.wdl:4475
    {"Mtl_k5", {}, 0x00000u, {198.0f, 199.0f, 199.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 29.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=216@c4_materials.wdl:3722; ambient_green=216@c4_materials.wdl:3723; ambient_blue=216@c4_materials.wdl:3724; diffuse_red=255@c4_materials.wdl:3725
    // diffuse_green=255@c4_materials.wdl:3726; diffuse_blue=255@c4_materials.wdl:3727; specular_red=0@c4_materials.wdl:3728; specular_green=0@c4_materials.wdl:3729
    // specular_blue=0@c4_materials.wdl:3730; emissive_red=0@c4_materials.wdl:3731; emissive_green=0@c4_materials.wdl:3732; emissive_blue=0@c4_materials.wdl:3733
    // power=0@c4_materials.wdl:3734; albedo=50@c4_materials.wdl:3735
    {"Mtl_k_eyaz", {}, 0x00000u, {216.0f, 216.0f, 216.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=123@c4_materials.wdl:2900; ambient_green=122@c4_materials.wdl:2901; ambient_blue=128@c4_materials.wdl:2902; diffuse_red=164@c4_materials.wdl:2903
    // diffuse_green=164@c4_materials.wdl:2904; diffuse_blue=164@c4_materials.wdl:2905; specular_red=0@c4_materials.wdl:2906; specular_green=0@c4_materials.wdl:2907
    // specular_blue=0@c4_materials.wdl:2908; emissive_red=0@c4_materials.wdl:2909; emissive_green=0@c4_materials.wdl:2910; emissive_blue=0@c4_materials.wdl:2911
    // power=0@c4_materials.wdl:2912; albedo=50@c4_materials.wdl:2913
    {"Mtl_kafa", {}, 0x00000u, {123.0f, 122.0f, 128.0f}, {164.0f, 164.0f, 164.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:806; ambient_green=255@c4_materials.wdl:807; ambient_blue=255@c4_materials.wdl:808; diffuse_red=255@c4_materials.wdl:809
    // diffuse_green=255@c4_materials.wdl:810; diffuse_blue=255@c4_materials.wdl:811; specular_red=65@c4_materials.wdl:812; specular_green=59@c4_materials.wdl:813
    // specular_blue=58@c4_materials.wdl:814; emissive_red=0@c4_materials.wdl:815; emissive_green=0@c4_materials.wdl:816; emissive_blue=0@c4_materials.wdl:817
    // power=0@c4_materials.wdl:818; albedo=50@c4_materials.wdl:819
    {"Mtl_KAOS_DRK", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {65.0f, 59.0f, 58.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:5406; ambient_green=207@c4_materials.wdl:5407; ambient_blue=209@c4_materials.wdl:5408; diffuse_red=255@c4_materials.wdl:5409
    // diffuse_green=255@c4_materials.wdl:5410; diffuse_blue=255@c4_materials.wdl:5411; specular_red=36@c4_materials.wdl:5412; specular_green=36@c4_materials.wdl:5413
    // specular_blue=36@c4_materials.wdl:5414; emissive_red=9@c4_materials.wdl:5415; emissive_green=9@c4_materials.wdl:5416; emissive_blue=9@c4_materials.wdl:5417
    // power=0@c4_materials.wdl:5418; albedo=36@c4_materials.wdl:5419
    {"Mtl_kap2", {}, 0x00000u, {207.0f, 207.0f, 209.0f}, {255.0f, 255.0f, 255.0f}, {36.0f, 36.0f, 36.0f}, {9.0f, 9.0f, 9.0f}, 0xfffu, 0.0f, 36.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=215@c4_materials.wdl:5507; ambient_green=230@c4_materials.wdl:5508; ambient_blue=255@c4_materials.wdl:5509; diffuse_red=255@c4_materials.wdl:5510
    // diffuse_green=255@c4_materials.wdl:5511; diffuse_blue=255@c4_materials.wdl:5512; specular_red=0@c4_materials.wdl:5513; specular_green=0@c4_materials.wdl:5514
    // specular_blue=0@c4_materials.wdl:5515; emissive_red=10@c4_materials.wdl:5516; emissive_green=10@c4_materials.wdl:5517; emissive_blue=16@c4_materials.wdl:5518
    // power=0@c4_materials.wdl:5519; albedo=-100@c4_materials.wdl:5520
    {"Mtl_kapsul1", {}, 0x00000u, {215.0f, 230.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {10.0f, 10.0f, 16.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:5474; ambient_green=255@c4_materials.wdl:5475; ambient_blue=253@c4_materials.wdl:5476; diffuse_red=253@c4_materials.wdl:5477
    // diffuse_green=253@c4_materials.wdl:5478; diffuse_blue=253@c4_materials.wdl:5479; specular_red=253@c4_materials.wdl:5480; specular_green=253@c4_materials.wdl:5481
    // specular_blue=251@c4_materials.wdl:5482; emissive_red=255@c4_materials.wdl:5483; emissive_green=255@c4_materials.wdl:5484; emissive_blue=255@c4_materials.wdl:5485
    // power=0@c4_materials.wdl:5486; albedo=147@c4_materials.wdl:5487
    {"Mtl_kapsul_ust2", {}, 0x00000u, {255.0f, 255.0f, 253.0f}, {253.0f, 253.0f, 253.0f}, {253.0f, 253.0f, 251.0f}, {255.0f, 255.0f, 255.0f}, 0xfffu, 0.0f, 147.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=177@c4_materials.wdl:2883; ambient_green=177@c4_materials.wdl:2884; ambient_blue=180@c4_materials.wdl:2885; diffuse_red=0@c4_materials.wdl:2886
    // diffuse_green=0@c4_materials.wdl:2887; diffuse_blue=0@c4_materials.wdl:2888; specular_red=0@c4_materials.wdl:2889; specular_green=0@c4_materials.wdl:2890
    // specular_blue=0@c4_materials.wdl:2891; emissive_red=0@c4_materials.wdl:2892; emissive_green=0@c4_materials.wdl:2893; emissive_blue=0@c4_materials.wdl:2894
    // power=0@c4_materials.wdl:2895; albedo=50@c4_materials.wdl:2896
    {"Mtl_KGS", {}, 0x00000u, {177.0f, 177.0f, 180.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=100@c4_materials.wdl:2851; ambient_green=111@c4_materials.wdl:2852; ambient_blue=114@c4_materials.wdl:2853; diffuse_red=178@c4_materials.wdl:2854
    // diffuse_green=192@c4_materials.wdl:2855; diffuse_blue=193@c4_materials.wdl:2856; specular_red=30@c4_materials.wdl:2857; specular_green=27@c4_materials.wdl:2858
    // specular_blue=27@c4_materials.wdl:2859; emissive_red=0@c4_materials.wdl:2860; emissive_green=0@c4_materials.wdl:2861; emissive_blue=0@c4_materials.wdl:2862
    // power=0@c4_materials.wdl:2863; albedo=50@c4_materials.wdl:2864
    {"Mtl_kon", {}, 0x00000u, {100.0f, 111.0f, 114.0f}, {178.0f, 192.0f, 193.0f}, {30.0f, 27.0f, 27.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=225@c4_materials.wdl:5019; ambient_green=225@c4_materials.wdl:5020; ambient_blue=225@c4_materials.wdl:5021; diffuse_red=5@c4_materials.wdl:5022
    // diffuse_green=5@c4_materials.wdl:5023; diffuse_blue=5@c4_materials.wdl:5024; specular_red=5@c4_materials.wdl:5025; specular_green=5@c4_materials.wdl:5026
    // specular_blue=5@c4_materials.wdl:5027; emissive_red=12@c4_materials.wdl:5028; emissive_green=12@c4_materials.wdl:5029; emissive_blue=12@c4_materials.wdl:5030
    // power=0@c4_materials.wdl:5031; albedo=111@c4_materials.wdl:5032
    {"Mtl_kop_kap", {}, 0x00000u, {225.0f, 225.0f, 225.0f}, {5.0f, 5.0f, 5.0f}, {5.0f, 5.0f, 5.0f}, {12.0f, 12.0f, 12.0f}, 0xfffu, 0.0f, 111.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=158@c4_materials.wdl:3756; ambient_green=158@c4_materials.wdl:3757; ambient_blue=160@c4_materials.wdl:3758; diffuse_red=255@c4_materials.wdl:3759
    // diffuse_green=255@c4_materials.wdl:3760; diffuse_blue=255@c4_materials.wdl:3761; specular_red=3@c4_materials.wdl:3762; specular_green=3@c4_materials.wdl:3763
    // specular_blue=3@c4_materials.wdl:3764; emissive_red=0@c4_materials.wdl:3765; emissive_green=0@c4_materials.wdl:3766; emissive_blue=0@c4_materials.wdl:3767
    // power=0@c4_materials.wdl:3768; albedo=124@c4_materials.wdl:3769
    {"Mtl_koru", {}, 0x00000u, {158.0f, 158.0f, 160.0f}, {255.0f, 255.0f, 255.0f}, {3.0f, 3.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 124.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:6490; ambient_green=255@c4_materials.wdl:6491; ambient_blue=255@c4_materials.wdl:6492; diffuse_red=255@c4_materials.wdl:6493
    // diffuse_green=255@c4_materials.wdl:6494; diffuse_blue=255@c4_materials.wdl:6495; specular_red=111@c4_materials.wdl:6496; specular_green=111@c4_materials.wdl:6497
    // specular_blue=114@c4_materials.wdl:6498; emissive_red=18@c4_materials.wdl:6499; emissive_green=19@c4_materials.wdl:6500; emissive_blue=18@c4_materials.wdl:6501
    // power=9@c4_materials.wdl:6502; albedo=2@c4_materials.wdl:6503
    {"Mtl_kule_tut", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {111.0f, 111.0f, 114.0f}, {18.0f, 19.0f, 18.0f}, 0xfffu, 9.0f, 2.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=126@c4_materials.wdl:5541; ambient_green=126@c4_materials.wdl:5542; ambient_blue=132@c4_materials.wdl:5543; diffuse_red=255@c4_materials.wdl:5544
    // diffuse_green=255@c4_materials.wdl:5545; diffuse_blue=255@c4_materials.wdl:5546; specular_red=4@c4_materials.wdl:5547; specular_green=0@c4_materials.wdl:5548
    // specular_blue=0@c4_materials.wdl:5549; emissive_red=3@c4_materials.wdl:5550; emissive_green=0@c4_materials.wdl:5551; emissive_blue=0@c4_materials.wdl:5552
    // power=0@c4_materials.wdl:5553; albedo=20@c4_materials.wdl:5554
    {"Mtl_kuleler", {}, 0x00000u, {126.0f, 126.0f, 132.0f}, {255.0f, 255.0f, 255.0f}, {4.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=149@c4_materials.wdl:5909; ambient_green=152@c4_materials.wdl:5910; ambient_blue=158@c4_materials.wdl:5911; diffuse_red=164@c4_materials.wdl:5912
    // diffuse_green=164@c4_materials.wdl:5913; diffuse_blue=164@c4_materials.wdl:5914; specular_red=0@c4_materials.wdl:5915; specular_green=0@c4_materials.wdl:5916
    // specular_blue=0@c4_materials.wdl:5917; emissive_red=0@c4_materials.wdl:5918; emissive_green=0@c4_materials.wdl:5919; emissive_blue=0@c4_materials.wdl:5920
    // power=0@c4_materials.wdl:5921; albedo=50@c4_materials.wdl:5922
    {"Mtl_kulube_cam", {}, 0x00000u, {149.0f, 152.0f, 158.0f}, {164.0f, 164.0f, 164.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=114@c4_materials.wdl:2550; ambient_green=138@c4_materials.wdl:2551; ambient_blue=96@c4_materials.wdl:2552; diffuse_red=255@c4_materials.wdl:2553
    // diffuse_green=255@c4_materials.wdl:2554; diffuse_blue=255@c4_materials.wdl:2555; specular_red=4@c4_materials.wdl:2556; specular_green=4@c4_materials.wdl:2557
    // specular_blue=4@c4_materials.wdl:2558; emissive_red=0@c4_materials.wdl:2559; emissive_green=0@c4_materials.wdl:2560; emissive_blue=0@c4_materials.wdl:2561
    // power=0@c4_materials.wdl:2562; albedo=75@c4_materials.wdl:2563
    {"Mtl_kun", {}, 0x00000u, {114.0f, 138.0f, 96.0f}, {255.0f, 255.0f, 255.0f}, {4.0f, 4.0f, 4.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 75.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:5171; ambient_green=230@c4_materials.wdl:5172; ambient_blue=230@c4_materials.wdl:5173; diffuse_red=255@c4_materials.wdl:5174
    // diffuse_green=255@c4_materials.wdl:5175; diffuse_blue=255@c4_materials.wdl:5176; specular_red=0@c4_materials.wdl:5177; specular_green=0@c4_materials.wdl:5178
    // specular_blue=0@c4_materials.wdl:5179; emissive_red=0@c4_materials.wdl:5180; emissive_green=0@c4_materials.wdl:5181; emissive_blue=0@c4_materials.wdl:5182
    // power=2@c4_materials.wdl:5183; albedo=86@c4_materials.wdl:5184
    {"Mtl_lastikler", {}, 0x00000u, {230.0f, 230.0f, 230.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 2.0f, 86.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=129@c4_materials.wdl:739; ambient_green=117@c4_materials.wdl:740; ambient_blue=111@c4_materials.wdl:741; diffuse_red=255@c4_materials.wdl:742
    // diffuse_green=255@c4_materials.wdl:743; diffuse_blue=255@c4_materials.wdl:744; specular_red=12@c4_materials.wdl:745; specular_green=3@c4_materials.wdl:746
    // specular_blue=0@c4_materials.wdl:747; emissive_red=0@c4_materials.wdl:748; emissive_green=0@c4_materials.wdl:749; emissive_blue=0@c4_materials.wdl:750
    // power=3@c4_materials.wdl:751; albedo=34@c4_materials.wdl:752
    {"Mtl_loop_reklam", {}, 0x00000u, {129.0f, 117.0f, 111.0f}, {255.0f, 255.0f, 255.0f}, {12.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 3.0f, 34.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=200@c4_materials.wdl:722; ambient_green=190@c4_materials.wdl:723; ambient_blue=190@c4_materials.wdl:724; diffuse_red=220@c4_materials.wdl:725
    // diffuse_green=220@c4_materials.wdl:726; diffuse_blue=220@c4_materials.wdl:727; specular_red=0@c4_materials.wdl:728; specular_green=0@c4_materials.wdl:729
    // specular_blue=0@c4_materials.wdl:730; emissive_red=0@c4_materials.wdl:731; emissive_green=12@c4_materials.wdl:732; emissive_blue=13@c4_materials.wdl:733
    // power=0@c4_materials.wdl:734; albedo=-100@c4_materials.wdl:735
    {"Mtl_loop_reklam_bez", {}, 0x00000u, {200.0f, 190.0f, 190.0f}, {220.0f, 220.0f, 220.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 12.0f, 13.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=175@c4_materials.wdl:3586; ambient_green=175@c4_materials.wdl:3587; ambient_blue=175@c4_materials.wdl:3588; diffuse_red=255@c4_materials.wdl:3589
    // diffuse_green=255@c4_materials.wdl:3590; diffuse_blue=255@c4_materials.wdl:3591; specular_red=0@c4_materials.wdl:3592; specular_green=0@c4_materials.wdl:3593
    // specular_blue=0@c4_materials.wdl:3594; emissive_red=0@c4_materials.wdl:3595; emissive_green=0@c4_materials.wdl:3596; emissive_blue=0@c4_materials.wdl:3597
    // power=0@c4_materials.wdl:3598; albedo=23@c4_materials.wdl:3599
    {"Mtl_luna", {}, 0x00000u, {175.0f, 175.0f, 175.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 23.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:685; ambient_green=206@c4_materials.wdl:686; ambient_blue=200@c4_materials.wdl:687; diffuse_red=255@c4_materials.wdl:688
    // diffuse_green=253@c4_materials.wdl:689; diffuse_blue=254@c4_materials.wdl:690; specular_red=59@c4_materials.wdl:691; specular_green=21@c4_materials.wdl:692
    // specular_blue=0@c4_materials.wdl:693; emissive_red=45@c4_materials.wdl:694; emissive_green=45@c4_materials.wdl:695; emissive_blue=18@c4_materials.wdl:696
    // power=0@c4_materials.wdl:697; albedo=9@c4_materials.wdl:698
    {"Mtl_MAR_BAR_2", {}, 0x00000u, {230.0f, 206.0f, 200.0f}, {255.0f, 253.0f, 254.0f}, {59.0f, 21.0f, 0.0f}, {45.0f, 45.0f, 18.0f}, 0xfffu, 0.0f, 9.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:670; ambient_green=136@c4_materials.wdl:671; ambient_blue=126@c4_materials.wdl:672; diffuse_red=255@c4_materials.wdl:673
    // diffuse_green=253@c4_materials.wdl:674; diffuse_blue=254@c4_materials.wdl:675; specular_red=0@c4_materials.wdl:676; specular_green=0@c4_materials.wdl:677
    // specular_blue=0@c4_materials.wdl:678; emissive_red=48@c4_materials.wdl:679; emissive_green=33@c4_materials.wdl:680; emissive_blue=24@c4_materials.wdl:681
    // power=0@c4_materials.wdl:682; albedo=5@c4_materials.wdl:683
    {"Mtl_MAR_BAR_TEL", {}, 0x00000u, {140.0f, 136.0f, 126.0f}, {255.0f, 253.0f, 254.0f}, {0.0f, 0.0f, 0.0f}, {48.0f, 33.0f, 24.0f}, 0xfffu, 0.0f, 5.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=125@c4_materials.wdl:1335; ambient_green=131@c4_materials.wdl:1336; ambient_blue=128@c4_materials.wdl:1337; diffuse_red=255@c4_materials.wdl:1338
    // diffuse_green=255@c4_materials.wdl:1339; diffuse_blue=255@c4_materials.wdl:1340; specular_red=6@c4_materials.wdl:1341; specular_green=3@c4_materials.wdl:1342
    // specular_blue=3@c4_materials.wdl:1343; emissive_red=0@c4_materials.wdl:1344; emissive_green=0@c4_materials.wdl:1345; emissive_blue=0@c4_materials.wdl:1346
    // power=0@c4_materials.wdl:1347; albedo=52@c4_materials.wdl:1348
    {"Mtl_mar_ip", {}, 0x00000u, {125.0f, 131.0f, 128.0f}, {255.0f, 255.0f, 255.0f}, {6.0f, 3.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 52.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=236@c4_materials.wdl:4309; ambient_green=198@c4_materials.wdl:4310; ambient_blue=198@c4_materials.wdl:4311; diffuse_red=242@c4_materials.wdl:4312
    // diffuse_green=199@c4_materials.wdl:4313; diffuse_blue=198@c4_materials.wdl:4314; specular_red=0@c4_materials.wdl:4315; specular_green=0@c4_materials.wdl:4316
    // specular_blue=0@c4_materials.wdl:4317; emissive_red=79@c4_materials.wdl:4318; emissive_green=35@c4_materials.wdl:4319; emissive_blue=22@c4_materials.wdl:4320
    // power=0@c4_materials.wdl:4321; albedo=-42@c4_materials.wdl:4322
    {"Mtl_marayspot", {}, 0x00000u, {236.0f, 198.0f, 198.0f}, {242.0f, 199.0f, 198.0f}, {0.0f, 0.0f, 0.0f}, {79.0f, 35.0f, 22.0f}, 0xfffu, 0.0f, -42.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=12@c4_materials.wdl:2735; ambient_green=12@c4_materials.wdl:2736; ambient_blue=24@c4_materials.wdl:2737; diffuse_red=251@c4_materials.wdl:2738
    // diffuse_green=251@c4_materials.wdl:2739; diffuse_blue=251@c4_materials.wdl:2740; specular_red=0@c4_materials.wdl:2741; specular_green=0@c4_materials.wdl:2742
    // specular_blue=0@c4_materials.wdl:2743; emissive_red=155@c4_materials.wdl:2744; emissive_green=105@c4_materials.wdl:2745; emissive_blue=51@c4_materials.wdl:2746
    // power=0@c4_materials.wdl:2747; albedo=-100@c4_materials.wdl:2748
    {"Mtl_metal", {}, 0x00000u, {12.0f, 12.0f, 24.0f}, {251.0f, 251.0f, 251.0f}, {0.0f, 0.0f, 0.0f}, {155.0f, 105.0f, 51.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=126@c4_materials.wdl:2816; ambient_green=126@c4_materials.wdl:2817; ambient_blue=132@c4_materials.wdl:2818; diffuse_red=255@c4_materials.wdl:2819
    // diffuse_green=255@c4_materials.wdl:2820; diffuse_blue=255@c4_materials.wdl:2821; specular_red=4@c4_materials.wdl:2822; specular_green=0@c4_materials.wdl:2823
    // specular_blue=0@c4_materials.wdl:2824; emissive_red=3@c4_materials.wdl:2825; emissive_green=0@c4_materials.wdl:2826; emissive_blue=0@c4_materials.wdl:2827
    // power=0@c4_materials.wdl:2828; albedo=20@c4_materials.wdl:2829
    {"Mtl_milyo", {}, 0x00000u, {126.0f, 126.0f, 132.0f}, {255.0f, 255.0f, 255.0f}, {4.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=158@c4_materials.wdl:3101; ambient_green=158@c4_materials.wdl:3102; ambient_blue=158@c4_materials.wdl:3103; diffuse_red=255@c4_materials.wdl:3104
    // diffuse_green=255@c4_materials.wdl:3105; diffuse_blue=255@c4_materials.wdl:3106; specular_red=0@c4_materials.wdl:3107; specular_green=0@c4_materials.wdl:3108
    // specular_blue=0@c4_materials.wdl:3109; emissive_red=0@c4_materials.wdl:3110; emissive_green=0@c4_materials.wdl:3111; emissive_blue=0@c4_materials.wdl:3112
    // power=0@c4_materials.wdl:3113; albedo=7@c4_materials.wdl:3114
    {"Mtl_NFS", {}, 0x00000u, {158.0f, 158.0f, 158.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 7.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=157@c4_materials.wdl:3135; ambient_green=157@c4_materials.wdl:3136; ambient_blue=157@c4_materials.wdl:3137; diffuse_red=195@c4_materials.wdl:3138
    // diffuse_green=195@c4_materials.wdl:3139; diffuse_blue=195@c4_materials.wdl:3140; specular_red=0@c4_materials.wdl:3141; specular_green=0@c4_materials.wdl:3142
    // specular_blue=0@c4_materials.wdl:3143; emissive_red=0@c4_materials.wdl:3144; emissive_green=0@c4_materials.wdl:3145; emissive_blue=0@c4_materials.wdl:3146
    // power=0@c4_materials.wdl:3147; albedo=31@c4_materials.wdl:3148
    {"Mtl_NFS2", {}, 0x00000u, {157.0f, 157.0f, 157.0f}, {195.0f, 195.0f, 195.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 31.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=219@c4_materials.wdl:6181; ambient_green=219@c4_materials.wdl:6182; ambient_blue=219@c4_materials.wdl:6183; diffuse_red=247@c4_materials.wdl:6184
    // diffuse_green=247@c4_materials.wdl:6185; diffuse_blue=247@c4_materials.wdl:6186; specular_red=30@c4_materials.wdl:6187; specular_green=30@c4_materials.wdl:6188
    // specular_blue=25@c4_materials.wdl:6189; emissive_red=0@c4_materials.wdl:6190; emissive_green=0@c4_materials.wdl:6191; emissive_blue=0@c4_materials.wdl:6192
    // power=1@c4_materials.wdl:6193; albedo=32@c4_materials.wdl:6194
    {"Mtl_orta_bariyel", {}, 0x00000u, {219.0f, 219.0f, 219.0f}, {247.0f, 247.0f, 247.0f}, {30.0f, 30.0f, 25.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 32.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=219@c4_materials.wdl:6317; ambient_green=219@c4_materials.wdl:6318; ambient_blue=219@c4_materials.wdl:6319; diffuse_red=247@c4_materials.wdl:6320
    // diffuse_green=247@c4_materials.wdl:6321; diffuse_blue=247@c4_materials.wdl:6322; specular_red=30@c4_materials.wdl:6323; specular_green=30@c4_materials.wdl:6324
    // specular_blue=25@c4_materials.wdl:6325; emissive_red=0@c4_materials.wdl:6326; emissive_green=0@c4_materials.wdl:6327; emissive_blue=0@c4_materials.wdl:6328
    // power=1@c4_materials.wdl:6329; albedo=32@c4_materials.wdl:6330
    {"Mtl_orta_bariyel2", {}, 0x00000u, {219.0f, 219.0f, 219.0f}, {247.0f, 247.0f, 247.0f}, {30.0f, 30.0f, 25.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 32.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=135@c4_materials.wdl:6364; ambient_green=134@c4_materials.wdl:6365; ambient_blue=132@c4_materials.wdl:6366; diffuse_red=255@c4_materials.wdl:6367
    // diffuse_green=255@c4_materials.wdl:6368; diffuse_blue=255@c4_materials.wdl:6369; specular_red=80@c4_materials.wdl:6370; specular_green=80@c4_materials.wdl:6371
    // specular_blue=80@c4_materials.wdl:6372; emissive_red=18@c4_materials.wdl:6373; emissive_green=18@c4_materials.wdl:6374; emissive_blue=18@c4_materials.wdl:6375
    // power=0@c4_materials.wdl:6376; albedo=167@c4_materials.wdl:6377
    {"Mtl_oto_lamb", {}, 0x00000u, {135.0f, 134.0f, 132.0f}, {255.0f, 255.0f, 255.0f}, {80.0f, 80.0f, 80.0f}, {18.0f, 18.0f, 18.0f}, 0xfffu, 0.0f, 167.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=90@c4_materials.wdl:1875; ambient_green=90@c4_materials.wdl:1876; ambient_blue=90@c4_materials.wdl:1877; diffuse_red=255@c4_materials.wdl:1878
    // diffuse_green=255@c4_materials.wdl:1879; diffuse_blue=255@c4_materials.wdl:1880; specular_red=0@c4_materials.wdl:1881; specular_green=0@c4_materials.wdl:1882
    // specular_blue=0@c4_materials.wdl:1883; emissive_red=18@c4_materials.wdl:1884; emissive_green=15@c4_materials.wdl:1885; emissive_blue=0@c4_materials.wdl:1886
    // power=4@c4_materials.wdl:1887; albedo=162@c4_materials.wdl:1888
    {"Mtl_parca_2", {}, 0x00000u, {90.0f, 90.0f, 90.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {18.0f, 15.0f, 0.0f}, 0xfffu, 4.0f, 162.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=51@c4_materials.wdl:2362; ambient_green=59@c4_materials.wdl:2363; ambient_blue=64@c4_materials.wdl:2364; diffuse_red=255@c4_materials.wdl:2365
    // diffuse_green=255@c4_materials.wdl:2366; diffuse_blue=255@c4_materials.wdl:2367; specular_red=12@c4_materials.wdl:2368; specular_green=12@c4_materials.wdl:2369
    // specular_blue=12@c4_materials.wdl:2370; emissive_red=25@c4_materials.wdl:2371; emissive_green=32@c4_materials.wdl:2372; emissive_blue=33@c4_materials.wdl:2373
    // power=0@c4_materials.wdl:2374; albedo=29@c4_materials.wdl:2375
    {"Mtl_park", {}, 0x00000u, {51.0f, 59.0f, 64.0f}, {255.0f, 255.0f, 255.0f}, {12.0f, 12.0f, 12.0f}, {25.0f, 32.0f, 33.0f}, 0xfffu, 0.0f, 29.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=12@c4_materials.wdl:2347; ambient_green=9@c4_materials.wdl:2348; ambient_blue=9@c4_materials.wdl:2349; diffuse_red=255@c4_materials.wdl:2350
    // diffuse_green=255@c4_materials.wdl:2351; diffuse_blue=255@c4_materials.wdl:2352; specular_red=0@c4_materials.wdl:2353; specular_green=0@c4_materials.wdl:2354
    // specular_blue=0@c4_materials.wdl:2355; emissive_red=0@c4_materials.wdl:2356; emissive_green=0@c4_materials.wdl:2357; emissive_blue=0@c4_materials.wdl:2358
    // power=0@c4_materials.wdl:2359; albedo=50@c4_materials.wdl:2360
    {"Mtl_park_kum", {}, 0x00000u, {12.0f, 9.0f, 9.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4262; ambient_green=255@c4_materials.wdl:4263; ambient_blue=255@c4_materials.wdl:4264; diffuse_red=207@c4_materials.wdl:4265
    // diffuse_green=199@c4_materials.wdl:4266; diffuse_blue=189@c4_materials.wdl:4267; specular_red=126@c4_materials.wdl:4268; specular_green=83@c4_materials.wdl:4269
    // specular_blue=21@c4_materials.wdl:4270; emissive_red=158@c4_materials.wdl:4271; emissive_green=80@c4_materials.wdl:4272; emissive_blue=33@c4_materials.wdl:4273
    // power=0@c4_materials.wdl:4274; albedo=-19@c4_materials.wdl:4275
    {"Mtl_pervane", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {207.0f, 199.0f, 189.0f}, {126.0f, 83.0f, 21.0f}, {158.0f, 80.0f, 33.0f}, 0xfffu, 0.0f, -19.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=238@c4_materials.wdl:3620; ambient_green=238@c4_materials.wdl:3621; ambient_blue=235@c4_materials.wdl:3622; diffuse_red=255@c4_materials.wdl:3623
    // diffuse_green=255@c4_materials.wdl:3624; diffuse_blue=255@c4_materials.wdl:3625; specular_red=7@c4_materials.wdl:3626; specular_green=6@c4_materials.wdl:3627
    // specular_blue=6@c4_materials.wdl:3628; emissive_red=0@c4_materials.wdl:3629; emissive_green=0@c4_materials.wdl:3630; emissive_blue=0@c4_materials.wdl:3631
    // power=0@c4_materials.wdl:3632; albedo=23@c4_materials.wdl:3633
    {"Mtl_POP", {}, 0x00000u, {238.0f, 238.0f, 235.0f}, {255.0f, 255.0f, 255.0f}, {7.0f, 6.0f, 6.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 23.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:5737; ambient_green=255@c4_materials.wdl:5738; ambient_blue=255@c4_materials.wdl:5739; diffuse_red=255@c4_materials.wdl:5740
    // diffuse_green=255@c4_materials.wdl:5741; diffuse_blue=255@c4_materials.wdl:5742; specular_red=3@c4_materials.wdl:5743; specular_green=3@c4_materials.wdl:5744
    // specular_blue=3@c4_materials.wdl:5745; emissive_red=0@c4_materials.wdl:5746; emissive_green=0@c4_materials.wdl:5747; emissive_blue=0@c4_materials.wdl:5748
    // power=0@c4_materials.wdl:5749; albedo=7@c4_materials.wdl:5750
    {"Mtl_rampa_01", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {3.0f, 3.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 7.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=177@c4_materials.wdl:5623; ambient_green=177@c4_materials.wdl:5624; ambient_blue=177@c4_materials.wdl:5625; diffuse_red=255@c4_materials.wdl:5626
    // diffuse_green=255@c4_materials.wdl:5627; diffuse_blue=255@c4_materials.wdl:5628; specular_red=27@c4_materials.wdl:5629; specular_green=27@c4_materials.wdl:5630
    // specular_blue=24@c4_materials.wdl:5631; emissive_red=3@c4_materials.wdl:5632; emissive_green=0@c4_materials.wdl:5633; emissive_blue=0@c4_materials.wdl:5634
    // power=1@c4_materials.wdl:5635; albedo=41@c4_materials.wdl:5636
    {"Mtl_robot_cs2", {}, 0x00000u, {177.0f, 177.0f, 177.0f}, {255.0f, 255.0f, 255.0f}, {27.0f, 27.0f, 24.0f}, {3.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=83@c4_materials.wdl:7011; ambient_green=71@c4_materials.wdl:7012; ambient_blue=56@c4_materials.wdl:7013; diffuse_red=119@c4_materials.wdl:7014
    // diffuse_green=116@c4_materials.wdl:7015; diffuse_blue=138@c4_materials.wdl:7016; specular_red=12@c4_materials.wdl:7017; specular_green=0@c4_materials.wdl:7018
    // specular_blue=0@c4_materials.wdl:7019; emissive_red=0@c4_materials.wdl:7020; emissive_green=0@c4_materials.wdl:7021; emissive_blue=0@c4_materials.wdl:7022
    // power=0@c4_materials.wdl:7023; albedo=50@c4_materials.wdl:7024
    {"Mtl_robot_fargovde", {}, 0x00000u, {83.0f, 71.0f, 56.0f}, {119.0f, 116.0f, 138.0f}, {12.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:7066; ambient_green=140@c4_materials.wdl:7067; ambient_blue=140@c4_materials.wdl:7068; diffuse_red=190@c4_materials.wdl:7069
    // diffuse_green=190@c4_materials.wdl:7070; diffuse_blue=190@c4_materials.wdl:7071; specular_red=20@c4_materials.wdl:7072; specular_green=18@c4_materials.wdl:7073
    // specular_blue=18@c4_materials.wdl:7074; emissive_red=18@c4_materials.wdl:7075; emissive_green=18@c4_materials.wdl:7076; emissive_blue=18@c4_materials.wdl:7077
    // power=10@c4_materials.wdl:7078; albedo=41@c4_materials.wdl:7079
    {"Mtl_robot_kaporta", {}, 0x00000u, {140.0f, 140.0f, 140.0f}, {190.0f, 190.0f, 190.0f}, {20.0f, 18.0f, 18.0f}, {18.0f, 18.0f, 18.0f}, 0xfffu, 10.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4007; ambient_green=255@c4_materials.wdl:4008; ambient_blue=255@c4_materials.wdl:4009; diffuse_red=255@c4_materials.wdl:4010
    // diffuse_green=255@c4_materials.wdl:4011; diffuse_blue=255@c4_materials.wdl:4012; specular_red=7@c4_materials.wdl:4013; specular_green=7@c4_materials.wdl:4014
    // specular_blue=7@c4_materials.wdl:4015; emissive_red=0@c4_materials.wdl:4016; emissive_green=0@c4_materials.wdl:4017; emissive_blue=0@c4_materials.wdl:4018
    // power=0@c4_materials.wdl:4019; albedo=58@c4_materials.wdl:4020
    {"Mtl_saat", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {7.0f, 7.0f, 7.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 58.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=157@c4_materials.wdl:2782; ambient_green=157@c4_materials.wdl:2783; ambient_blue=154@c4_materials.wdl:2784; diffuse_red=255@c4_materials.wdl:2785
    // diffuse_green=255@c4_materials.wdl:2786; diffuse_blue=255@c4_materials.wdl:2787; specular_red=0@c4_materials.wdl:2788; specular_green=0@c4_materials.wdl:2789
    // specular_blue=0@c4_materials.wdl:2790; emissive_red=0@c4_materials.wdl:2791; emissive_green=0@c4_materials.wdl:2792; emissive_blue=0@c4_materials.wdl:2793
    // power=0@c4_materials.wdl:2794; albedo=49@c4_materials.wdl:2795
    {"Mtl_sahil", {}, 0x00000u, {157.0f, 157.0f, 154.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 49.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4902; ambient_green=255@c4_materials.wdl:4903; ambient_blue=255@c4_materials.wdl:4904; diffuse_red=30@c4_materials.wdl:4905
    // diffuse_green=30@c4_materials.wdl:4906; diffuse_blue=30@c4_materials.wdl:4907; specular_red=0@c4_materials.wdl:4908; specular_green=0@c4_materials.wdl:4909
    // specular_blue=0@c4_materials.wdl:4910; emissive_red=0@c4_materials.wdl:4911; emissive_green=0@c4_materials.wdl:4912; emissive_blue=0@c4_materials.wdl:4913
    // power=0@c4_materials.wdl:4914; albedo=50@c4_materials.wdl:4915
    {"Mtl_sarmasik", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {30.0f, 30.0f, 30.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=102@c4_materials.wdl:6625; ambient_green=98@c4_materials.wdl:6626; ambient_blue=90@c4_materials.wdl:6627; diffuse_red=99@c4_materials.wdl:6628
    // diffuse_green=99@c4_materials.wdl:6629; diffuse_blue=99@c4_materials.wdl:6630; specular_red=22@c4_materials.wdl:6631; specular_green=22@c4_materials.wdl:6632
    // specular_blue=22@c4_materials.wdl:6633; emissive_red=33@c4_materials.wdl:6634; emissive_green=33@c4_materials.wdl:6635; emissive_blue=33@c4_materials.wdl:6636
    // power=10@c4_materials.wdl:6637; albedo=60@c4_materials.wdl:6638
    {"Mtl_semsiye", {}, 0x00000u, {102.0f, 98.0f, 90.0f}, {99.0f, 99.0f, 99.0f}, {22.0f, 22.0f, 22.0f}, {33.0f, 33.0f, 33.0f}, 0xfffu, 10.0f, 60.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=116@c4_materials.wdl:1845; ambient_green=116@c4_materials.wdl:1846; ambient_blue=116@c4_materials.wdl:1847; diffuse_red=160@c4_materials.wdl:1848
    // diffuse_green=158@c4_materials.wdl:1849; diffuse_blue=158@c4_materials.wdl:1850; specular_red=0@c4_materials.wdl:1851; specular_green=0@c4_materials.wdl:1852
    // specular_blue=0@c4_materials.wdl:1853; emissive_red=0@c4_materials.wdl:1854; emissive_green=0@c4_materials.wdl:1855; emissive_blue=0@c4_materials.wdl:1856
    // power=0@c4_materials.wdl:1857; albedo=50@c4_materials.wdl:1858
    {"Mtl_serit", {}, 0x00000u, {116.0f, 116.0f, 116.0f}, {160.0f, 158.0f, 158.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=189@c4_materials.wdl:5036; ambient_green=195@c4_materials.wdl:5037; ambient_blue=198@c4_materials.wdl:5038; diffuse_red=241@c4_materials.wdl:5039
    // diffuse_green=253@c4_materials.wdl:5040; diffuse_blue=255@c4_materials.wdl:5041; specular_red=21@c4_materials.wdl:5042; specular_green=24@c4_materials.wdl:5043
    // specular_blue=32@c4_materials.wdl:5044; emissive_red=0@c4_materials.wdl:5045; emissive_green=0@c4_materials.wdl:5046; emissive_blue=0@c4_materials.wdl:5047
    // power=1@c4_materials.wdl:5048; albedo=5@c4_materials.wdl:5049
    {"Mtl_set1", {}, 0x00000u, {189.0f, 195.0f, 198.0f}, {241.0f, 253.0f, 255.0f}, {21.0f, 24.0f, 32.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 5.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=195@c4_materials.wdl:601; ambient_green=186@c4_materials.wdl:602; ambient_blue=174@c4_materials.wdl:603; diffuse_red=255@c4_materials.wdl:604
    // diffuse_green=254@c4_materials.wdl:605; diffuse_blue=254@c4_materials.wdl:606; specular_red=15@c4_materials.wdl:607; specular_green=0@c4_materials.wdl:608
    // specular_blue=0@c4_materials.wdl:609; emissive_red=0@c4_materials.wdl:610; emissive_green=0@c4_materials.wdl:611; emissive_blue=0@c4_materials.wdl:612
    // power=0@c4_materials.wdl:613; albedo=41@c4_materials.wdl:614
    {"Mtl_set_dur", {}, 0x00000u, {195.0f, 186.0f, 174.0f}, {255.0f, 254.0f, 254.0f}, {15.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=154@c4_materials.wdl:3523; ambient_green=154@c4_materials.wdl:3524; ambient_blue=165@c4_materials.wdl:3525; diffuse_red=255@c4_materials.wdl:3526
    // diffuse_green=255@c4_materials.wdl:3527; diffuse_blue=255@c4_materials.wdl:3528; specular_red=0@c4_materials.wdl:3529; specular_green=0@c4_materials.wdl:3530
    // specular_blue=0@c4_materials.wdl:3531; emissive_red=0@c4_materials.wdl:3532; emissive_green=0@c4_materials.wdl:3533; emissive_blue=0@c4_materials.wdl:3534
    // power=0@c4_materials.wdl:3535; albedo=50@c4_materials.wdl:3536
    {"Mtl_SH_anayol", {}, 0x00000u, {154.0f, 154.0f, 165.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=235@c4_materials.wdl:3475; ambient_green=235@c4_materials.wdl:3476; ambient_blue=241@c4_materials.wdl:3477; diffuse_red=255@c4_materials.wdl:3478
    // diffuse_green=255@c4_materials.wdl:3479; diffuse_blue=255@c4_materials.wdl:3480; specular_red=0@c4_materials.wdl:3481; specular_green=0@c4_materials.wdl:3482
    // specular_blue=0@c4_materials.wdl:3483; emissive_red=0@c4_materials.wdl:3484; emissive_green=0@c4_materials.wdl:3485; emissive_blue=0@c4_materials.wdl:3486
    // power=0@c4_materials.wdl:3487; albedo=7@c4_materials.wdl:3488
    {"Mtl_SH_beton", {}, 0x00000u, {235.0f, 235.0f, 241.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 7.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=154@c4_materials.wdl:2253; ambient_green=154@c4_materials.wdl:2254; ambient_blue=165@c4_materials.wdl:2255; diffuse_red=255@c4_materials.wdl:2256
    // diffuse_green=255@c4_materials.wdl:2257; diffuse_blue=255@c4_materials.wdl:2258; specular_red=0@c4_materials.wdl:2259; specular_green=0@c4_materials.wdl:2260
    // specular_blue=0@c4_materials.wdl:2261; emissive_red=0@c4_materials.wdl:2262; emissive_green=0@c4_materials.wdl:2263; emissive_blue=0@c4_materials.wdl:2264
    // power=0@c4_materials.wdl:2265; albedo=50@c4_materials.wdl:2266
    {"Mtl_SH_denekop_ek", {}, 0x00000u, {154.0f, 154.0f, 165.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=154@c4_materials.wdl:2301; ambient_green=154@c4_materials.wdl:2302; ambient_blue=165@c4_materials.wdl:2303; diffuse_red=255@c4_materials.wdl:2304
    // diffuse_green=255@c4_materials.wdl:2305; diffuse_blue=255@c4_materials.wdl:2306; specular_red=0@c4_materials.wdl:2307; specular_green=0@c4_materials.wdl:2308
    // specular_blue=0@c4_materials.wdl:2309; emissive_red=0@c4_materials.wdl:2310; emissive_green=0@c4_materials.wdl:2311; emissive_blue=0@c4_materials.wdl:2312
    // power=0@c4_materials.wdl:2313; albedo=50@c4_materials.wdl:2314
    {"Mtl_SH_ex_yol", {}, 0x00000u, {154.0f, 154.0f, 165.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=228@c4_materials.wdl:3297; ambient_green=228@c4_materials.wdl:3298; ambient_blue=228@c4_materials.wdl:3299; diffuse_red=255@c4_materials.wdl:3300
    // diffuse_green=255@c4_materials.wdl:3301; diffuse_blue=255@c4_materials.wdl:3302; specular_red=0@c4_materials.wdl:3303; specular_green=0@c4_materials.wdl:3304
    // specular_blue=0@c4_materials.wdl:3305; emissive_red=0@c4_materials.wdl:3306; emissive_green=0@c4_materials.wdl:3307; emissive_blue=0@c4_materials.wdl:3308
    // power=0@c4_materials.wdl:3309; albedo=50@c4_materials.wdl:3310
    {"Mtl_SH_gecis", {}, 0x00000u, {228.0f, 228.0f, 228.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=175@c4_materials.wdl:3427; ambient_green=175@c4_materials.wdl:3428; ambient_blue=175@c4_materials.wdl:3429; diffuse_red=255@c4_materials.wdl:3430
    // diffuse_green=255@c4_materials.wdl:3431; diffuse_blue=255@c4_materials.wdl:3432; specular_red=0@c4_materials.wdl:3433; specular_green=0@c4_materials.wdl:3434
    // specular_blue=0@c4_materials.wdl:3435; emissive_red=0@c4_materials.wdl:3436; emissive_green=0@c4_materials.wdl:3437; emissive_blue=0@c4_materials.wdl:3438
    // power=0@c4_materials.wdl:3439; albedo=7@c4_materials.wdl:3440
    {"Mtl_SH_kule", {}, 0x00000u, {175.0f, 175.0f, 175.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 7.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=175@c4_materials.wdl:3379; ambient_green=175@c4_materials.wdl:3380; ambient_blue=175@c4_materials.wdl:3381; diffuse_red=255@c4_materials.wdl:3382
    // diffuse_green=255@c4_materials.wdl:3383; diffuse_blue=255@c4_materials.wdl:3384; specular_red=0@c4_materials.wdl:3385; specular_green=0@c4_materials.wdl:3386
    // specular_blue=0@c4_materials.wdl:3387; emissive_red=0@c4_materials.wdl:3388; emissive_green=0@c4_materials.wdl:3389; emissive_blue=0@c4_materials.wdl:3390
    // power=0@c4_materials.wdl:3391; albedo=7@c4_materials.wdl:3392
    {"Mtl_SH_kule2", {}, 0x00000u, {175.0f, 175.0f, 175.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 7.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_SH_zgisealt", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No recovered material definition; all scalar fields unknown.
    {"Mtl_ShaderEditor", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:6449; ambient_green=255@c4_materials.wdl:6450; ambient_blue=255@c4_materials.wdl:6451; diffuse_red=255@c4_materials.wdl:6452
    // diffuse_green=255@c4_materials.wdl:6453; diffuse_blue=255@c4_materials.wdl:6454; specular_red=111@c4_materials.wdl:6455; specular_green=111@c4_materials.wdl:6456
    // specular_blue=114@c4_materials.wdl:6457; emissive_red=18@c4_materials.wdl:6458; emissive_green=19@c4_materials.wdl:6459; emissive_blue=18@c4_materials.wdl:6460
    // power=9@c4_materials.wdl:6461; albedo=2@c4_materials.wdl:6462
    {"Mtl_silindir", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {111.0f, 111.0f, 114.0f}, {18.0f, 19.0f, 18.0f}, 0xfffu, 9.0f, 2.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=210@c4_materials.wdl:5068; ambient_green=210@c4_materials.wdl:5069; ambient_blue=210@c4_materials.wdl:5070; diffuse_red=100@c4_materials.wdl:5071
    // diffuse_green=100@c4_materials.wdl:5072; diffuse_blue=40@c4_materials.wdl:5073; specular_red=0@c4_materials.wdl:5074; specular_green=0@c4_materials.wdl:5075
    // specular_blue=0@c4_materials.wdl:5076; emissive_red=0@c4_materials.wdl:5077; emissive_green=0@c4_materials.wdl:5078; emissive_blue=0@c4_materials.wdl:5079
    // power=0@c4_materials.wdl:5080; albedo=50@c4_materials.wdl:5081
    {"Mtl_sobe_yan", {}, 0x00000u, {210.0f, 210.0f, 210.0f}, {100.0f, 100.0f, 40.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=253@c4_materials.wdl:1350; ambient_green=56@c4_materials.wdl:1351; ambient_blue=164@c4_materials.wdl:1352; diffuse_red=255@c4_materials.wdl:1353
    // diffuse_green=254@c4_materials.wdl:1354; diffuse_blue=250@c4_materials.wdl:1355; specular_red=0@c4_materials.wdl:1356; specular_green=1@c4_materials.wdl:1357
    // specular_blue=0@c4_materials.wdl:1358; emissive_red=41@c4_materials.wdl:1359; emissive_green=56@c4_materials.wdl:1360; emissive_blue=22@c4_materials.wdl:1361
    // power=0@c4_materials.wdl:1362; albedo=104@c4_materials.wdl:1363
    {"Mtl_spooot", {}, 0x00000u, {253.0f, 56.0f, 164.0f}, {255.0f, 254.0f, 250.0f}, {0.0f, 1.0f, 0.0f}, {41.0f, 56.0f, 22.0f}, 0xfffu, 0.0f, 104.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:3790; ambient_green=255@c4_materials.wdl:3791; ambient_blue=255@c4_materials.wdl:3792; diffuse_red=255@c4_materials.wdl:3793
    // diffuse_green=255@c4_materials.wdl:3794; diffuse_blue=255@c4_materials.wdl:3795; specular_red=0@c4_materials.wdl:3796; specular_green=0@c4_materials.wdl:3797
    // specular_blue=0@c4_materials.wdl:3798; emissive_red=0@c4_materials.wdl:3799; emissive_green=0@c4_materials.wdl:3800; emissive_blue=0@c4_materials.wdl:3801
    // power=0@c4_materials.wdl:3802; albedo=9@c4_materials.wdl:3803
    {"Mtl_st_layt", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 9.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=222@c4_materials.wdl:5272; ambient_green=236@c4_materials.wdl:5273; ambient_blue=255@c4_materials.wdl:5274; diffuse_red=255@c4_materials.wdl:5275
    // diffuse_green=255@c4_materials.wdl:5276; diffuse_blue=255@c4_materials.wdl:5277; specular_red=0@c4_materials.wdl:5278; specular_green=0@c4_materials.wdl:5279
    // specular_blue=0@c4_materials.wdl:5280; emissive_red=7@c4_materials.wdl:5281; emissive_green=7@c4_materials.wdl:5282; emissive_blue=7@c4_materials.wdl:5283
    // power=0@c4_materials.wdl:5284; albedo=50@c4_materials.wdl:5285
    {"Mtl_stop", {}, 0x00000u, {222.0f, 236.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {7.0f, 7.0f, 7.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=175@c4_materials.wdl:3688; ambient_green=184@c4_materials.wdl:3689; ambient_blue=189@c4_materials.wdl:3690; diffuse_red=255@c4_materials.wdl:3691
    // diffuse_green=255@c4_materials.wdl:3692; diffuse_blue=255@c4_materials.wdl:3693; specular_red=48@c4_materials.wdl:3694; specular_green=42@c4_materials.wdl:3695
    // specular_blue=38@c4_materials.wdl:3696; emissive_red=21@c4_materials.wdl:3697; emissive_green=15@c4_materials.wdl:3698; emissive_blue=12@c4_materials.wdl:3699
    // power=0@c4_materials.wdl:3700; albedo=-100@c4_materials.wdl:3701
    {"Mtl_tabela_8_9", {}, 0x00000u, {175.0f, 184.0f, 189.0f}, {255.0f, 255.0f, 255.0f}, {48.0f, 42.0f, 38.0f}, {21.0f, 15.0f, 12.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=87@c4_materials.wdl:3197; ambient_green=87@c4_materials.wdl:3198; ambient_blue=87@c4_materials.wdl:3199; diffuse_red=137@c4_materials.wdl:3200
    // diffuse_green=135@c4_materials.wdl:3201; diffuse_blue=135@c4_materials.wdl:3202; specular_red=22@c4_materials.wdl:3203; specular_green=18@c4_materials.wdl:3204
    // specular_blue=18@c4_materials.wdl:3205; emissive_red=19@c4_materials.wdl:3206; emissive_green=19@c4_materials.wdl:3207; emissive_blue=19@c4_materials.wdl:3208
    // power=0@c4_materials.wdl:3209; albedo=40@c4_materials.wdl:3210
    {"Mtl_takim", {}, 0x00000u, {87.0f, 87.0f, 87.0f}, {137.0f, 135.0f, 135.0f}, {22.0f, 18.0f, 18.0f}, {19.0f, 19.0f, 19.0f}, 0xfffu, 0.0f, 40.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=154@c4_materials.wdl:2933; ambient_green=166@c4_materials.wdl:2934; ambient_blue=170@c4_materials.wdl:2935; diffuse_red=255@c4_materials.wdl:2936
    // diffuse_green=255@c4_materials.wdl:2937; diffuse_blue=255@c4_materials.wdl:2938; specular_red=0@c4_materials.wdl:2939; specular_green=0@c4_materials.wdl:2940
    // specular_blue=0@c4_materials.wdl:2941; emissive_red=0@c4_materials.wdl:2942; emissive_green=0@c4_materials.wdl:2943; emissive_blue=0@c4_materials.wdl:2944
    // power=0@c4_materials.wdl:2945; albedo=50@c4_materials.wdl:2946
    {"Mtl_tel_vs", {}, 0x00000u, {154.0f, 166.0f, 170.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=204@c4_materials.wdl:2469; ambient_green=199@c4_materials.wdl:2470; ambient_blue=196@c4_materials.wdl:2471; diffuse_red=255@c4_materials.wdl:2472
    // diffuse_green=255@c4_materials.wdl:2473; diffuse_blue=255@c4_materials.wdl:2474; specular_red=1@c4_materials.wdl:2475; specular_green=0@c4_materials.wdl:2476
    // specular_blue=0@c4_materials.wdl:2477; emissive_red=0@c4_materials.wdl:2478; emissive_green=0@c4_materials.wdl:2479; emissive_blue=0@c4_materials.wdl:2480
    // power=0@c4_materials.wdl:2481; albedo=41@c4_materials.wdl:2482
    {"Mtl_temel", {}, 0x00000u, {204.0f, 199.0f, 196.0f}, {255.0f, 255.0f, 255.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=141@c4_materials.wdl:1206; ambient_green=244@c4_materials.wdl:1207; ambient_blue=244@c4_materials.wdl:1208; diffuse_red=203@c4_materials.wdl:1209
    // diffuse_green=199@c4_materials.wdl:1210; diffuse_blue=196@c4_materials.wdl:1211; specular_red=247@c4_materials.wdl:1212; specular_green=244@c4_materials.wdl:1213
    // specular_blue=247@c4_materials.wdl:1214; emissive_red=137@c4_materials.wdl:1215; emissive_green=155@c4_materials.wdl:1216; emissive_blue=155@c4_materials.wdl:1217
    // power=4@c4_materials.wdl:1218; albedo=20@c4_materials.wdl:1219
    {"Mtl_tentem", {}, 0x00000u, {141.0f, 244.0f, 244.0f}, {203.0f, 199.0f, 196.0f}, {247.0f, 244.0f, 247.0f}, {137.0f, 155.0f, 155.0f}, 0xfffu, 4.0f, 20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=222@c4_materials.wdl:5355; ambient_green=222@c4_materials.wdl:5356; ambient_blue=233@c4_materials.wdl:5357; diffuse_red=13@c4_materials.wdl:5358
    // diffuse_green=12@c4_materials.wdl:5359; diffuse_blue=12@c4_materials.wdl:5360; specular_red=0@c4_materials.wdl:5361; specular_green=0@c4_materials.wdl:5362
    // specular_blue=0@c4_materials.wdl:5363; emissive_red=29@c4_materials.wdl:5364; emissive_green=12@c4_materials.wdl:5365; emissive_blue=19@c4_materials.wdl:5366
    // power=0@c4_materials.wdl:5367; albedo=-100@c4_materials.wdl:5368
    {"Mtl_test_ag", {}, 0x00000u, {222.0f, 222.0f, 233.0f}, {13.0f, 12.0f, 12.0f}, {0.0f, 0.0f, 0.0f}, {29.0f, 12.0f, 19.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=161@c4_materials.wdl:4496; ambient_green=160@c4_materials.wdl:4497; ambient_blue=163@c4_materials.wdl:4498; diffuse_red=195@c4_materials.wdl:4499
    // diffuse_green=195@c4_materials.wdl:4500; diffuse_blue=198@c4_materials.wdl:4501; specular_red=0@c4_materials.wdl:4502; specular_green=0@c4_materials.wdl:4503
    // specular_blue=0@c4_materials.wdl:4504; emissive_red=0@c4_materials.wdl:4505; emissive_green=0@c4_materials.wdl:4506; emissive_blue=0@c4_materials.wdl:4507
    // power=0@c4_materials.wdl:4508; albedo=50@c4_materials.wdl:4509
    {"Mtl_topak_y", {}, 0x00000u, {161.0f, 160.0f, 163.0f}, {195.0f, 195.0f, 198.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=206@c4_materials.wdl:4360; ambient_green=206@c4_materials.wdl:4361; ambient_blue=209@c4_materials.wdl:4362; diffuse_red=242@c4_materials.wdl:4363
    // diffuse_green=242@c4_materials.wdl:4364; diffuse_blue=242@c4_materials.wdl:4365; specular_red=0@c4_materials.wdl:4366; specular_green=0@c4_materials.wdl:4367
    // specular_blue=0@c4_materials.wdl:4368; emissive_red=0@c4_materials.wdl:4369; emissive_green=0@c4_materials.wdl:4370; emissive_blue=0@c4_materials.wdl:4371
    // power=2@c4_materials.wdl:4372; albedo=23@c4_materials.wdl:4373
    {"Mtl_topkapi", {}, 0x00000u, {206.0f, 206.0f, 209.0f}, {242.0f, 242.0f, 242.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 2.0f, 23.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=174@c4_materials.wdl:1406; ambient_green=157@c4_materials.wdl:1407; ambient_blue=70@c4_materials.wdl:1408; diffuse_red=255@c4_materials.wdl:1409
    // diffuse_green=255@c4_materials.wdl:1410; diffuse_blue=255@c4_materials.wdl:1411; specular_red=0@c4_materials.wdl:1412; specular_green=1@c4_materials.wdl:1413
    // specular_blue=0@c4_materials.wdl:1414; emissive_red=7@c4_materials.wdl:1415; emissive_green=4@c4_materials.wdl:1416; emissive_blue=16@c4_materials.wdl:1417
    // power=0@c4_materials.wdl:1418; albedo=-85@c4_materials.wdl:1419
    {"Mtl_tozcuk", {}, 0x00000u, {174.0f, 157.0f, 70.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 1.0f, 0.0f}, {7.0f, 4.0f, 16.0f}, 0xfffu, 0.0f, -85.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_traf_levha", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_tunel", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=175@c4_materials.wdl:1945; ambient_green=175@c4_materials.wdl:1946; ambient_blue=175@c4_materials.wdl:1947; diffuse_red=255@c4_materials.wdl:1948
    // diffuse_green=255@c4_materials.wdl:1949; diffuse_blue=255@c4_materials.wdl:1950; specular_red=0@c4_materials.wdl:1951; specular_green=0@c4_materials.wdl:1952
    // specular_blue=0@c4_materials.wdl:1953; emissive_red=0@c4_materials.wdl:1954; emissive_green=0@c4_materials.wdl:1955; emissive_blue=0@c4_materials.wdl:1956
    // power=0@c4_materials.wdl:1957; albedo=50@c4_materials.wdl:1958
    {"Mtl_tup_kald", {}, 0x00000u, {175.0f, 175.0f, 175.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=165@c4_materials.wdl:6820; ambient_green=165@c4_materials.wdl:6821; ambient_blue=165@c4_materials.wdl:6822; diffuse_red=255@c4_materials.wdl:6823
    // diffuse_green=255@c4_materials.wdl:6824; diffuse_blue=255@c4_materials.wdl:6825; specular_red=0@c4_materials.wdl:6826; specular_green=0@c4_materials.wdl:6827
    // specular_blue=0@c4_materials.wdl:6828; emissive_red=0@c4_materials.wdl:6829; emissive_green=0@c4_materials.wdl:6830; emissive_blue=0@c4_materials.wdl:6831
    // power=0@c4_materials.wdl:6832; albedo=50@c4_materials.wdl:6833
    {"Mtl_tup_kald_dis", {}, 0x00000u, {165.0f, 165.0f, 165.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=165@c4_materials.wdl:6786; ambient_green=165@c4_materials.wdl:6787; ambient_blue=165@c4_materials.wdl:6788; diffuse_red=255@c4_materials.wdl:6789
    // diffuse_green=255@c4_materials.wdl:6790; diffuse_blue=255@c4_materials.wdl:6791; specular_red=0@c4_materials.wdl:6792; specular_green=0@c4_materials.wdl:6793
    // specular_blue=0@c4_materials.wdl:6794; emissive_red=0@c4_materials.wdl:6795; emissive_green=0@c4_materials.wdl:6796; emissive_blue=0@c4_materials.wdl:6797
    // power=0@c4_materials.wdl:6798; albedo=50@c4_materials.wdl:6799
    {"Mtl_tup_kald_son", {}, 0x00000u, {165.0f, 165.0f, 165.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:5440; ambient_green=207@c4_materials.wdl:5441; ambient_blue=209@c4_materials.wdl:5442; diffuse_red=255@c4_materials.wdl:5443
    // diffuse_green=255@c4_materials.wdl:5444; diffuse_blue=255@c4_materials.wdl:5445; specular_red=36@c4_materials.wdl:5446; specular_green=36@c4_materials.wdl:5447
    // specular_blue=36@c4_materials.wdl:5448; emissive_red=9@c4_materials.wdl:5449; emissive_green=9@c4_materials.wdl:5450; emissive_blue=9@c4_materials.wdl:5451
    // power=0@c4_materials.wdl:5452; albedo=36@c4_materials.wdl:5453
    {"Mtl_tup_tunel_son", {}, 0x00000u, {207.0f, 207.0f, 209.0f}, {255.0f, 255.0f, 255.0f}, {36.0f, 36.0f, 36.0f}, {9.0f, 9.0f, 9.0f}, 0xfffu, 0.0f, 36.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=160@c4_materials.wdl:1979; ambient_green=160@c4_materials.wdl:1980; ambient_blue=160@c4_materials.wdl:1981; diffuse_red=255@c4_materials.wdl:1982
    // diffuse_green=255@c4_materials.wdl:1983; diffuse_blue=255@c4_materials.wdl:1984; specular_red=0@c4_materials.wdl:1985; specular_green=0@c4_materials.wdl:1986
    // specular_blue=0@c4_materials.wdl:1987; emissive_red=0@c4_materials.wdl:1988; emissive_green=0@c4_materials.wdl:1989; emissive_blue=0@c4_materials.wdl:1990
    // power=0@c4_materials.wdl:1991; albedo=50@c4_materials.wdl:1992
    {"Mtl_tup_yol", {}, 0x00000u, {160.0f, 160.0f, 160.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=160@c4_materials.wdl:6750; ambient_green=160@c4_materials.wdl:6751; ambient_blue=160@c4_materials.wdl:6752; diffuse_red=255@c4_materials.wdl:6753
    // diffuse_green=255@c4_materials.wdl:6754; diffuse_blue=255@c4_materials.wdl:6755; specular_red=0@c4_materials.wdl:6756; specular_green=0@c4_materials.wdl:6757
    // specular_blue=0@c4_materials.wdl:6758; emissive_red=0@c4_materials.wdl:6759; emissive_green=0@c4_materials.wdl:6760; emissive_blue=0@c4_materials.wdl:6761
    // power=0@c4_materials.wdl:6762; albedo=50@c4_materials.wdl:6763
    {"Mtl_tup_yol_dis", {}, 0x00000u, {160.0f, 160.0f, 160.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=160@c4_materials.wdl:6716; ambient_green=160@c4_materials.wdl:6717; ambient_blue=160@c4_materials.wdl:6718; diffuse_red=255@c4_materials.wdl:6719
    // diffuse_green=255@c4_materials.wdl:6720; diffuse_blue=255@c4_materials.wdl:6721; specular_red=0@c4_materials.wdl:6722; specular_green=0@c4_materials.wdl:6723
    // specular_blue=0@c4_materials.wdl:6724; emissive_red=0@c4_materials.wdl:6725; emissive_green=0@c4_materials.wdl:6726; emissive_blue=0@c4_materials.wdl:6727
    // power=0@c4_materials.wdl:6728; albedo=50@c4_materials.wdl:6729
    {"Mtl_tup_yol_son", {}, 0x00000u, {160.0f, 160.0f, 160.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:3569; ambient_green=255@c4_materials.wdl:3570; ambient_blue=255@c4_materials.wdl:3571; diffuse_red=20@c4_materials.wdl:3572
    // diffuse_green=20@c4_materials.wdl:3573; diffuse_blue=20@c4_materials.wdl:3574; specular_red=13@c4_materials.wdl:3575; specular_green=13@c4_materials.wdl:3576
    // specular_blue=13@c4_materials.wdl:3577; emissive_red=50@c4_materials.wdl:3578; emissive_green=50@c4_materials.wdl:3579; emissive_blue=50@c4_materials.wdl:3580
    // power=0@c4_materials.wdl:3581; albedo=56@c4_materials.wdl:3582
    {"Mtl_turbo", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {20.0f, 20.0f, 20.0f}, {13.0f, 13.0f, 13.0f}, {50.0f, 50.0f, 50.0f}, 0xfffu, 0.0f, 56.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=219@c4_materials.wdl:6283; ambient_green=219@c4_materials.wdl:6284; ambient_blue=219@c4_materials.wdl:6285; diffuse_red=247@c4_materials.wdl:6286
    // diffuse_green=247@c4_materials.wdl:6287; diffuse_blue=247@c4_materials.wdl:6288; specular_red=30@c4_materials.wdl:6289; specular_green=30@c4_materials.wdl:6290
    // specular_blue=25@c4_materials.wdl:6291; emissive_red=0@c4_materials.wdl:6292; emissive_green=0@c4_materials.wdl:6293; emissive_blue=0@c4_materials.wdl:6294
    // power=1@c4_materials.wdl:6295; albedo=32@c4_materials.wdl:6296
    {"Mtl_uc_bariyer", {}, 0x00000u, {219.0f, 219.0f, 219.0f}, {247.0f, 247.0f, 247.0f}, {30.0f, 30.0f, 25.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 32.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=206@c4_materials.wdl:3654; ambient_green=203@c4_materials.wdl:3655; ambient_blue=189@c4_materials.wdl:3656; diffuse_red=255@c4_materials.wdl:3657
    // diffuse_green=255@c4_materials.wdl:3658; diffuse_blue=255@c4_materials.wdl:3659; specular_red=1@c4_materials.wdl:3660; specular_green=1@c4_materials.wdl:3661
    // specular_blue=0@c4_materials.wdl:3662; emissive_red=12@c4_materials.wdl:3663; emissive_green=9@c4_materials.wdl:3664; emissive_blue=9@c4_materials.wdl:3665
    // power=1@c4_materials.wdl:3666; albedo=52@c4_materials.wdl:3667
    {"Mtl_uc_ok", {}, 0x00000u, {206.0f, 203.0f, 189.0f}, {255.0f, 255.0f, 255.0f}, {1.0f, 1.0f, 0.0f}, {12.0f, 9.0f, 9.0f}, 0xfffu, 1.0f, 52.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:3892; ambient_green=255@c4_materials.wdl:3893; ambient_blue=255@c4_materials.wdl:3894; diffuse_red=90@c4_materials.wdl:3895
    // diffuse_green=90@c4_materials.wdl:3896; diffuse_blue=93@c4_materials.wdl:3897; specular_red=0@c4_materials.wdl:3898; specular_green=0@c4_materials.wdl:3899
    // specular_blue=0@c4_materials.wdl:3900; emissive_red=0@c4_materials.wdl:3901; emissive_green=0@c4_materials.wdl:3902; emissive_blue=0@c4_materials.wdl:3903
    // power=0@c4_materials.wdl:3904; albedo=2@c4_materials.wdl:3905
    {"Mtl_ucgen", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {90.0f, 90.0f, 93.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 2.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=204@c4_materials.wdl:3858; ambient_green=204@c4_materials.wdl:3859; ambient_blue=204@c4_materials.wdl:3860; diffuse_red=255@c4_materials.wdl:3861
    // diffuse_green=255@c4_materials.wdl:3862; diffuse_blue=255@c4_materials.wdl:3863; specular_red=0@c4_materials.wdl:3864; specular_green=0@c4_materials.wdl:3865
    // specular_blue=0@c4_materials.wdl:3866; emissive_red=0@c4_materials.wdl:3867; emissive_green=0@c4_materials.wdl:3868; emissive_blue=0@c4_materials.wdl:3869
    // power=0@c4_materials.wdl:3870; albedo=50@c4_materials.wdl:3871
    {"Mtl_ucgen_2", {}, 0x00000u, {204.0f, 204.0f, 204.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=161@c4_materials.wdl:2109; ambient_green=160@c4_materials.wdl:2110; ambient_blue=163@c4_materials.wdl:2111; diffuse_red=195@c4_materials.wdl:2112
    // diffuse_green=195@c4_materials.wdl:2113; diffuse_blue=198@c4_materials.wdl:2114; specular_red=0@c4_materials.wdl:2115; specular_green=0@c4_materials.wdl:2116
    // specular_blue=0@c4_materials.wdl:2117; emissive_red=0@c4_materials.wdl:2118; emissive_green=0@c4_materials.wdl:2119; emissive_blue=0@c4_materials.wdl:2120
    // power=0@c4_materials.wdl:2121; albedo=50@c4_materials.wdl:2122
    {"Mtl_vadi", {}, 0x00000u, {161.0f, 160.0f, 163.0f}, {195.0f, 195.0f, 198.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=161@c4_materials.wdl:2061; ambient_green=160@c4_materials.wdl:2062; ambient_blue=163@c4_materials.wdl:2063; diffuse_red=195@c4_materials.wdl:2064
    // diffuse_green=195@c4_materials.wdl:2065; diffuse_blue=198@c4_materials.wdl:2066; specular_red=0@c4_materials.wdl:2067; specular_green=0@c4_materials.wdl:2068
    // specular_blue=0@c4_materials.wdl:2069; emissive_red=0@c4_materials.wdl:2070; emissive_green=0@c4_materials.wdl:2071; emissive_blue=0@c4_materials.wdl:2072
    // power=0@c4_materials.wdl:2073; albedo=50@c4_materials.wdl:2074
    {"Mtl_vadi_yan", {}, 0x00000u, {161.0f, 160.0f, 163.0f}, {195.0f, 195.0f, 198.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=131@c4_materials.wdl:1672; ambient_green=128@c4_materials.wdl:1673; ambient_blue=129@c4_materials.wdl:1674; diffuse_red=120@c4_materials.wdl:1675
    // diffuse_green=120@c4_materials.wdl:1676; diffuse_blue=114@c4_materials.wdl:1677; specular_red=15@c4_materials.wdl:1678; specular_green=15@c4_materials.wdl:1679
    // specular_blue=12@c4_materials.wdl:1680; emissive_red=0@c4_materials.wdl:1681; emissive_green=0@c4_materials.wdl:1682; emissive_blue=0@c4_materials.wdl:1683
    // power=0@c4_materials.wdl:1684; albedo=75@c4_materials.wdl:1685
    {"Mtl_villa_citler", {}, 0x00000u, {131.0f, 128.0f, 129.0f}, {120.0f, 120.0f, 114.0f}, {15.0f, 15.0f, 12.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 75.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=143@c4_materials.wdl:821; ambient_green=146@c4_materials.wdl:822; ambient_blue=149@c4_materials.wdl:823; diffuse_red=255@c4_materials.wdl:824
    // diffuse_green=255@c4_materials.wdl:825; diffuse_blue=255@c4_materials.wdl:826; specular_red=29@c4_materials.wdl:827; specular_green=30@c4_materials.wdl:828
    // specular_blue=24@c4_materials.wdl:829; emissive_red=45@c4_materials.wdl:830; emissive_green=38@c4_materials.wdl:831; emissive_blue=25@c4_materials.wdl:832
    // power=0@c4_materials.wdl:833; albedo=50@c4_materials.wdl:834
    {"Mtl_vinc_mod", {}, 0x00000u, {143.0f, 146.0f, 149.0f}, {255.0f, 255.0f, 255.0f}, {29.0f, 30.0f, 24.0f}, {45.0f, 38.0f, 25.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=222@c4_materials.wdl:6066; ambient_green=222@c4_materials.wdl:6067; ambient_blue=222@c4_materials.wdl:6068; diffuse_red=125@c4_materials.wdl:6069
    // diffuse_green=126@c4_materials.wdl:6070; diffuse_blue=126@c4_materials.wdl:6071; specular_red=0@c4_materials.wdl:6072; specular_green=0@c4_materials.wdl:6073
    // specular_blue=0@c4_materials.wdl:6074; emissive_red=0@c4_materials.wdl:6075; emissive_green=0@c4_materials.wdl:6076; emissive_blue=0@c4_materials.wdl:6077
    // power=0@c4_materials.wdl:6078; albedo=50@c4_materials.wdl:6079
    {"Mtl_w_duvar", {}, 0x00000u, {222.0f, 222.0f, 222.0f}, {125.0f, 126.0f, 126.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=219@c4_materials.wdl:6249; ambient_green=219@c4_materials.wdl:6250; ambient_blue=219@c4_materials.wdl:6251; diffuse_red=247@c4_materials.wdl:6252
    // diffuse_green=247@c4_materials.wdl:6253; diffuse_blue=247@c4_materials.wdl:6254; specular_red=30@c4_materials.wdl:6255; specular_green=30@c4_materials.wdl:6256
    // specular_blue=25@c4_materials.wdl:6257; emissive_red=0@c4_materials.wdl:6258; emissive_green=0@c4_materials.wdl:6259; emissive_blue=0@c4_materials.wdl:6260
    // power=1@c4_materials.wdl:6261; albedo=32@c4_materials.wdl:6262
    {"Mtl_willa_kapi", {}, 0x00000u, {219.0f, 219.0f, 219.0f}, {247.0f, 247.0f, 247.0f}, {30.0f, 30.0f, 25.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 32.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=161@c4_materials.wdl:2013; ambient_green=160@c4_materials.wdl:2014; ambient_blue=163@c4_materials.wdl:2015; diffuse_red=195@c4_materials.wdl:2016
    // diffuse_green=195@c4_materials.wdl:2017; diffuse_blue=198@c4_materials.wdl:2018; specular_red=0@c4_materials.wdl:2019; specular_green=0@c4_materials.wdl:2020
    // specular_blue=0@c4_materials.wdl:2021; emissive_red=0@c4_materials.wdl:2022; emissive_green=0@c4_materials.wdl:2023; emissive_blue=0@c4_materials.wdl:2024
    // power=0@c4_materials.wdl:2025; albedo=50@c4_materials.wdl:2026
    {"Mtl_y_kum_yan", {}, 0x00000u, {161.0f, 160.0f, 163.0f}, {195.0f, 195.0f, 198.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=77@c4_materials.wdl:5836; ambient_green=77@c4_materials.wdl:5837; ambient_blue=77@c4_materials.wdl:5838; diffuse_red=111@c4_materials.wdl:5839
    // diffuse_green=111@c4_materials.wdl:5840; diffuse_blue=111@c4_materials.wdl:5841; specular_red=45@c4_materials.wdl:5842; specular_green=47@c4_materials.wdl:5843
    // specular_blue=47@c4_materials.wdl:5844; emissive_red=0@c4_materials.wdl:5845; emissive_green=0@c4_materials.wdl:5846; emissive_blue=0@c4_materials.wdl:5847
    // power=0@c4_materials.wdl:5848; albedo=50@c4_materials.wdl:5849
    {"Mtl_yan_oklar", {}, 0x00000u, {77.0f, 77.0f, 77.0f}, {111.0f, 111.0f, 111.0f}, {45.0f, 47.0f, 47.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // No scalar source writes; all scalar fields unassigned.
    {"Mtl_yantel", {}, 0x00000u, {}, {}, {}, {}, 0x000u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=232@c4_materials.wdl:1657; ambient_green=226@c4_materials.wdl:1658; ambient_blue=226@c4_materials.wdl:1659; diffuse_red=170@c4_materials.wdl:1660
    // diffuse_green=172@c4_materials.wdl:1661; diffuse_blue=164@c4_materials.wdl:1662; specular_red=10@c4_materials.wdl:1663; specular_green=10@c4_materials.wdl:1664
    // specular_blue=10@c4_materials.wdl:1665; emissive_red=32@c4_materials.wdl:1666; emissive_green=21@c4_materials.wdl:1667; emissive_blue=10@c4_materials.wdl:1668
    // power=0@c4_materials.wdl:1669; albedo=32@c4_materials.wdl:1670
    {"Mtl_yeni_agac", {}, 0x00000u, {232.0f, 226.0f, 226.0f}, {170.0f, 172.0f, 164.0f}, {10.0f, 10.0f, 10.0f}, {32.0f, 21.0f, 10.0f}, 0xfffu, 0.0f, 32.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=114@c4_materials.wdl:1245; ambient_green=70@c4_materials.wdl:1246; ambient_blue=58@c4_materials.wdl:1247; diffuse_red=255@c4_materials.wdl:1248
    // diffuse_green=255@c4_materials.wdl:1249; diffuse_blue=255@c4_materials.wdl:1250; specular_red=0@c4_materials.wdl:1251; specular_green=0@c4_materials.wdl:1252
    // specular_blue=0@c4_materials.wdl:1253; emissive_red=0@c4_materials.wdl:1254; emissive_green=0@c4_materials.wdl:1255; emissive_blue=0@c4_materials.wdl:1256
    // power=0@c4_materials.wdl:1257; albedo=-65@c4_materials.wdl:1258
    {"Mtl_yeni_atmos_sag", {}, 0x00000u, {114.0f, 70.0f, 58.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, -65.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=67@c4_materials.wdl:1291; ambient_green=54@c4_materials.wdl:1292; ambient_blue=50@c4_materials.wdl:1293; diffuse_red=255@c4_materials.wdl:1294
    // diffuse_green=251@c4_materials.wdl:1295; diffuse_blue=251@c4_materials.wdl:1296; specular_red=9@c4_materials.wdl:1297; specular_green=0@c4_materials.wdl:1298
    // specular_blue=0@c4_materials.wdl:1299; emissive_red=22@c4_materials.wdl:1300; emissive_green=6@c4_materials.wdl:1301; emissive_blue=4@c4_materials.wdl:1302
    // power=0@c4_materials.wdl:1303; albedo=-53@c4_materials.wdl:1304
    {"Mtl_yeni_atmos_sol", {}, 0x00000u, {67.0f, 54.0f, 50.0f}, {255.0f, 251.0f, 251.0f}, {9.0f, 0.0f, 0.0f}, {22.0f, 6.0f, 4.0f}, 0xfffu, 0.0f, -53.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=208@c4_materials.wdl:4294; ambient_green=208@c4_materials.wdl:4295; ambient_blue=208@c4_materials.wdl:4296; diffuse_red=0@c4_materials.wdl:4297
    // diffuse_green=12@c4_materials.wdl:4298; diffuse_blue=0@c4_materials.wdl:4299; specular_red=157@c4_materials.wdl:4300; specular_green=157@c4_materials.wdl:4301
    // specular_blue=180@c4_materials.wdl:4302; emissive_red=80@c4_materials.wdl:4303; emissive_green=47@c4_materials.wdl:4304; emissive_blue=47@c4_materials.wdl:4305
    // power=0@c4_materials.wdl:4306; albedo=20@c4_materials.wdl:4307
    {"Mtl_yeni_bor", {}, 0x00000u, {208.0f, 208.0f, 208.0f}, {0.0f, 12.0f, 0.0f}, {157.0f, 157.0f, 180.0f}, {80.0f, 47.0f, 47.0f}, 0xfffu, 0.0f, 20.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=250@c4_materials.wdl:1625; ambient_green=241@c4_materials.wdl:1626; ambient_blue=235@c4_materials.wdl:1627; diffuse_red=248@c4_materials.wdl:1628
    // diffuse_green=255@c4_materials.wdl:1629; diffuse_blue=255@c4_materials.wdl:1630; specular_red=36@c4_materials.wdl:1631; specular_green=18@c4_materials.wdl:1632
    // specular_blue=0@c4_materials.wdl:1633; emissive_red=0@c4_materials.wdl:1634; emissive_green=0@c4_materials.wdl:1635; emissive_blue=0@c4_materials.wdl:1636
    // power=0@c4_materials.wdl:1637; albedo=40@c4_materials.wdl:1638
    {"Mtl_yeni_pal", {}, 0x00000u, {250.0f, 241.0f, 235.0f}, {248.0f, 255.0f, 255.0f}, {36.0f, 18.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 40.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=122@c4_materials.wdl:1591; ambient_green=122@c4_materials.wdl:1592; ambient_blue=112@c4_materials.wdl:1593; diffuse_red=53@c4_materials.wdl:1594
    // diffuse_green=48@c4_materials.wdl:1595; diffuse_blue=56@c4_materials.wdl:1596; specular_red=0@c4_materials.wdl:1597; specular_green=0@c4_materials.wdl:1598
    // specular_blue=4@c4_materials.wdl:1599; emissive_red=0@c4_materials.wdl:1600; emissive_green=0@c4_materials.wdl:1601; emissive_blue=0@c4_materials.wdl:1602
    // power=0@c4_materials.wdl:1603; albedo=-29@c4_materials.wdl:1604
    {"Mtl_yeni_siteler", {}, 0x00000u, {122.0f, 122.0f, 112.0f}, {53.0f, 48.0f, 56.0f}, {0.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, -29.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=161@c4_materials.wdl:1897; ambient_green=160@c4_materials.wdl:1898; ambient_blue=163@c4_materials.wdl:1899; diffuse_red=195@c4_materials.wdl:1900
    // diffuse_green=195@c4_materials.wdl:1901; diffuse_blue=198@c4_materials.wdl:1902; specular_red=0@c4_materials.wdl:1903; specular_green=0@c4_materials.wdl:1904
    // specular_blue=0@c4_materials.wdl:1905; emissive_red=0@c4_materials.wdl:1906; emissive_green=0@c4_materials.wdl:1907; emissive_blue=0@c4_materials.wdl:1908
    // power=0@c4_materials.wdl:1909; albedo=50@c4_materials.wdl:1910
    {"Mtl_yol_kum", {}, 0x00000u, {161.0f, 160.0f, 163.0f}, {195.0f, 195.0f, 198.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=216@c4_materials.wdl:5098; ambient_green=210@c4_materials.wdl:5099; ambient_blue=207@c4_materials.wdl:5100; diffuse_red=225@c4_materials.wdl:5101
    // diffuse_green=225@c4_materials.wdl:5102; diffuse_blue=235@c4_materials.wdl:5103; specular_red=0@c4_materials.wdl:5104; specular_green=0@c4_materials.wdl:5105
    // specular_blue=15@c4_materials.wdl:5106; emissive_red=18@c4_materials.wdl:5107; emissive_green=15@c4_materials.wdl:5108; emissive_blue=15@c4_materials.wdl:5109
    // power=0@c4_materials.wdl:5110; albedo=36@c4_materials.wdl:5111
    {"Mtl_YOL_LAMB", {}, 0x00000u, {216.0f, 210.0f, 207.0f}, {225.0f, 225.0f, 235.0f}, {0.0f, 0.0f, 15.0f}, {18.0f, 15.0f, 15.0f}, 0xfffu, 0.0f, 36.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=131@c4_materials.wdl:4987; ambient_green=131@c4_materials.wdl:4988; ambient_blue=131@c4_materials.wdl:4989; diffuse_red=255@c4_materials.wdl:4990
    // diffuse_green=255@c4_materials.wdl:4991; diffuse_blue=255@c4_materials.wdl:4992; specular_red=29@c4_materials.wdl:4993; specular_green=29@c4_materials.wdl:4994
    // specular_blue=27@c4_materials.wdl:4995; emissive_red=0@c4_materials.wdl:4996; emissive_green=0@c4_materials.wdl:4997; emissive_blue=0@c4_materials.wdl:4998
    // power=0@c4_materials.wdl:4999; albedo=108@c4_materials.wdl:5000
    {"Mtl_yol_ok", {}, 0x00000u, {131.0f, 131.0f, 131.0f}, {255.0f, 255.0f, 255.0f}, {29.0f, 29.0f, 27.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 108.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=0@c4_materials.wdl:1228; ambient_green=204@c4_materials.wdl:1229; ambient_blue=4@c4_materials.wdl:1230; diffuse_red=250@c4_materials.wdl:1231
    // diffuse_green=255@c4_materials.wdl:1232; diffuse_blue=242@c4_materials.wdl:1233; specular_red=0@c4_materials.wdl:1234; specular_green=0@c4_materials.wdl:1235
    // specular_blue=0@c4_materials.wdl:1236; emissive_red=7@c4_materials.wdl:1237; emissive_green=4@c4_materials.wdl:1238; emissive_blue=0@c4_materials.wdl:1239
    // power=0@c4_materials.wdl:1240; albedo=-100@c4_materials.wdl:1241
    {"Mtl_yukari_ok", {}, 0x00000u, {0.0f, 204.0f, 4.0f}, {250.0f, 255.0f, 242.0f}, {0.0f, 0.0f, 0.0f}, {7.0f, 4.0f, 0.0f}, 0xfffu, 0.0f, -100.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=219@c4_materials.wdl:6215; ambient_green=219@c4_materials.wdl:6216; ambient_blue=219@c4_materials.wdl:6217; diffuse_red=247@c4_materials.wdl:6218
    // diffuse_green=247@c4_materials.wdl:6219; diffuse_blue=247@c4_materials.wdl:6220; specular_red=30@c4_materials.wdl:6221; specular_green=30@c4_materials.wdl:6222
    // specular_blue=25@c4_materials.wdl:6223; emissive_red=0@c4_materials.wdl:6224; emissive_green=0@c4_materials.wdl:6225; emissive_blue=0@c4_materials.wdl:6226
    // power=1@c4_materials.wdl:6227; albedo=32@c4_materials.wdl:6228
    {"Mtl_z_bariyerler", {}, 0x00000u, {219.0f, 219.0f, 219.0f}, {247.0f, 247.0f, 247.0f}, {30.0f, 30.0f, 25.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 32.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=228@c4_materials.wdl:5983; ambient_green=228@c4_materials.wdl:5984; ambient_blue=228@c4_materials.wdl:5985; diffuse_red=255@c4_materials.wdl:5986
    // diffuse_green=255@c4_materials.wdl:5987; diffuse_blue=255@c4_materials.wdl:5988; specular_red=0@c4_materials.wdl:5989; specular_green=0@c4_materials.wdl:5990
    // specular_blue=0@c4_materials.wdl:5991; emissive_red=0@c4_materials.wdl:5992; emissive_green=0@c4_materials.wdl:5993; emissive_blue=0@c4_materials.wdl:5994
    // power=0@c4_materials.wdl:5995; albedo=50@c4_materials.wdl:5996
    {"Mtl_z_kulube2", {}, 0x00000u, {228.0f, 228.0f, 228.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=222@c4_materials.wdl:1443; ambient_green=216@c4_materials.wdl:1444; ambient_blue=212@c4_materials.wdl:1445; diffuse_red=255@c4_materials.wdl:1446
    // diffuse_green=255@c4_materials.wdl:1447; diffuse_blue=255@c4_materials.wdl:1448; specular_red=90@c4_materials.wdl:1449; specular_green=74@c4_materials.wdl:1450
    // specular_blue=68@c4_materials.wdl:1451; emissive_red=0@c4_materials.wdl:1452; emissive_green=0@c4_materials.wdl:1453; emissive_blue=0@c4_materials.wdl:1454
    // power=0@c4_materials.wdl:1455; albedo=169@c4_materials.wdl:1456
    {"Mtl_ZAMANLAMA", {}, 0x00000u, {222.0f, 216.0f, 212.0f}, {255.0f, 255.0f, 255.0f}, {90.0f, 74.0f, 68.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 169.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=137@c4_materials.wdl:3824; ambient_green=138@c4_materials.wdl:3825; ambient_blue=149@c4_materials.wdl:3826; diffuse_red=255@c4_materials.wdl:3827
    // diffuse_green=255@c4_materials.wdl:3828; diffuse_blue=255@c4_materials.wdl:3829; specular_red=35@c4_materials.wdl:3830; specular_green=33@c4_materials.wdl:3831
    // specular_blue=47@c4_materials.wdl:3832; emissive_red=9@c4_materials.wdl:3833; emissive_green=9@c4_materials.wdl:3834; emissive_blue=9@c4_materials.wdl:3835
    // power=0@c4_materials.wdl:3836; albedo=32@c4_materials.wdl:3837
    {"Mtl_zeplin", {}, 0x00000u, {137.0f, 138.0f, 149.0f}, {255.0f, 255.0f, 255.0f}, {35.0f, 33.0f, 47.0f}, {9.0f, 9.0f, 9.0f}, 0xfffu, 0.0f, 32.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=180@c4_materials.wdl:1020; ambient_green=175@c4_materials.wdl:1021; ambient_blue=170@c4_materials.wdl:1022; diffuse_red=190@c4_materials.wdl:1023
    // diffuse_green=190@c4_materials.wdl:1024; diffuse_blue=190@c4_materials.wdl:1025; specular_red=30@c4_materials.wdl:1026; specular_green=30@c4_materials.wdl:1027
    // specular_blue=30@c4_materials.wdl:1028; emissive_red=8@c4_materials.wdl:1029; emissive_green=8@c4_materials.wdl:1030; emissive_blue=8@c4_materials.wdl:1031
    // power=10@c4_materials.wdl:1032; albedo=41@c4_materials.wdl:1033
    {"Mtl_kul_car", {}, 0x00000u, {180.0f, 175.0f, 170.0f}, {190.0f, 190.0f, 190.0f}, {30.0f, 30.0f, 30.0f}, {8.0f, 8.0f, 8.0f}, 0xfffu, 10.0f, 41.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:1067; ambient_green=140@c4_materials.wdl:1068; ambient_blue=140@c4_materials.wdl:1069; diffuse_red=200@c4_materials.wdl:1070
    // diffuse_green=200@c4_materials.wdl:1071; diffuse_blue=200@c4_materials.wdl:1072; specular_red=30@c4_materials.wdl:1073; specular_green=30@c4_materials.wdl:1074
    // specular_blue=30@c4_materials.wdl:1075; emissive_red=10@c4_materials.wdl:1076; emissive_green=10@c4_materials.wdl:1077; emissive_blue=10@c4_materials.wdl:1078
    // power=0@c4_materials.wdl:1079; albedo=15@c4_materials.wdl:1080
    {"Mtl_kul_robot", {}, 0x00000u, {140.0f, 140.0f, 140.0f}, {200.0f, 200.0f, 200.0f}, {30.0f, 30.0f, 30.0f}, {10.0f, 10.0f, 10.0f}, 0xfffu, 0.0f, 15.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=239@c4_materials.wdl:1114; ambient_green=238@c4_materials.wdl:1115; ambient_blue=235@c4_materials.wdl:1116; diffuse_red=241@c4_materials.wdl:1117
    // diffuse_green=245@c4_materials.wdl:1118; diffuse_blue=242@c4_materials.wdl:1119; specular_red=30@c4_materials.wdl:1120; specular_green=30@c4_materials.wdl:1121
    // specular_blue=30@c4_materials.wdl:1122; emissive_red=4@c4_materials.wdl:1123; emissive_green=4@c4_materials.wdl:1124; emissive_blue=0@c4_materials.wdl:1125
    // power=1@c4_materials.wdl:1126; albedo=104@c4_materials.wdl:1127
    {"Mtl_siyah_car", {}, 0x00000u, {239.0f, 238.0f, 235.0f}, {241.0f, 245.0f, 242.0f}, {30.0f, 30.0f, 30.0f}, {4.0f, 4.0f, 0.0f}, 0xfffu, 1.0f, 104.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 0; payload shared where selected fields are identical.
    // ambient_red=183@c4_materials.wdl:1161; ambient_green=188@c4_materials.wdl:1162; ambient_blue=185@c4_materials.wdl:1163; diffuse_red=290@c4_materials.wdl:1164
    // diffuse_green=290@c4_materials.wdl:1165; diffuse_blue=290@c4_materials.wdl:1166; specular_red=30@c4_materials.wdl:1167; specular_green=30@c4_materials.wdl:1168
    // specular_blue=30@c4_materials.wdl:1169; emissive_red=15@c4_materials.wdl:1170; emissive_green=15@c4_materials.wdl:1171; emissive_blue=15@c4_materials.wdl:1172
    // power=0@c4_materials.wdl:1173; albedo=15@c4_materials.wdl:1174
    {"Mtl_siyah_robot", {}, 0x00000u, {183.0f, 188.0f, 185.0f}, {290.0f, 290.0f, 290.0f}, {30.0f, 30.0f, 30.0f}, {15.0f, 15.0f, 15.0f}, 0xfffu, 0.0f, 15.0f, true, true, 0u, false, 0.0f, false},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:845; ambient_green=233@c4_materials.wdl:846; ambient_blue=209@c4_materials.wdl:847; diffuse_red=255@c4_materials.wdl:848
    // diffuse_green=255@c4_materials.wdl:849; diffuse_blue=255@c4_materials.wdl:850; specular_red=74@c4_materials.wdl:851; specular_green=0@c4_materials.wdl:852
    // specular_blue=18@c4_materials.wdl:853; emissive_red=0@c4_materials.wdl:854; emissive_green=0@c4_materials.wdl:855; emissive_blue=19@c4_materials.wdl:856
    // power=0@c4_materials.wdl:857; albedo=50@c4_materials.wdl:858; skill1=pixel_for_vec(vector(0,0,0),2.993,8888)@c4_materials.wdl:863
    {"Mtl_afis_site", {}, 0x00000u, {255.0f, 233.0f, 209.0f}, {255.0f, 255.0f, 255.0f}, {74.0f, 0.0f, 18.0f}, {0.0f, 0.0f, 19.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 2.9921875f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=71@c4_materials.wdl:7042; ambient_green=68@c4_materials.wdl:7043; ambient_blue=76@c4_materials.wdl:7044; diffuse_red=76@c4_materials.wdl:7045
    // diffuse_green=82@c4_materials.wdl:7046; diffuse_blue=83@c4_materials.wdl:7047; specular_red=0@c4_materials.wdl:7048; specular_green=1@c4_materials.wdl:7049
    // specular_blue=0@c4_materials.wdl:7050; emissive_red=0@c4_materials.wdl:7051; emissive_green=4@c4_materials.wdl:7052; emissive_blue=1@c4_materials.wdl:7053
    // power=0@c4_materials.wdl:7054; albedo=-28@c4_materials.wdl:7055; skill1=pixel_for_vec(vector(0,0,0),66.44,8888)@c4_materials.wdl:7060
    {"Mtl_arac_cam", {}, 0x00000u, {71.0f, 68.0f, 76.0f}, {76.0f, 82.0f, 83.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 4.0f, 1.0f}, 0xfffu, 0.0f, -28.0f, true, true, 0u, false, 66.439453125f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=39@c4_materials.wdl:6954; ambient_green=42@c4_materials.wdl:6955; ambient_blue=42@c4_materials.wdl:6956; diffuse_red=0@c4_materials.wdl:6957
    // diffuse_green=0@c4_materials.wdl:6958; diffuse_blue=0@c4_materials.wdl:6959; specular_red=137@c4_materials.wdl:6960; specular_green=87@c4_materials.wdl:6961
    // specular_blue=0@c4_materials.wdl:6962; emissive_red=0@c4_materials.wdl:6963; emissive_green=0@c4_materials.wdl:6964; emissive_blue=0@c4_materials.wdl:6965
    // power=0@c4_materials.wdl:6966; albedo=16@c4_materials.wdl:6967; skill1=pixel_for_vec(vector(0,0,0),52.68,8888)@c4_materials.wdl:6972
    {"Mtl_arac_farcam", {}, 0x00000u, {39.0f, 42.0f, 42.0f}, {0.0f, 0.0f, 0.0f}, {137.0f, 87.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 16.0f, true, true, 0u, false, 52.6796875f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:648; ambient_green=145@c4_materials.wdl:649; ambient_blue=145@c4_materials.wdl:650; diffuse_red=255@c4_materials.wdl:651
    // diffuse_green=255@c4_materials.wdl:652; diffuse_blue=255@c4_materials.wdl:653; specular_red=0@c4_materials.wdl:654; specular_green=0@c4_materials.wdl:655
    // specular_blue=0@c4_materials.wdl:656; emissive_red=10@c4_materials.wdl:657; emissive_green=10@c4_materials.wdl:658; emissive_blue=9@c4_materials.wdl:659
    // power=0@c4_materials.wdl:660; albedo=-20@c4_materials.wdl:661; skill1=pixel_for_vec(vector(0,0,0),15,8888)@c4_materials.wdl:666
    {"Mtl_arka_far", {}, 0x00000u, {145.0f, 145.0f, 145.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {10.0f, 10.0f, 9.0f}, 0xfffu, 0.0f, -20.0f, true, true, 0u, false, 15.0f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=116@c4_materials.wdl:700; ambient_green=106@c4_materials.wdl:701; ambient_blue=103@c4_materials.wdl:702; diffuse_red=244@c4_materials.wdl:703
    // diffuse_green=232@c4_materials.wdl:704; diffuse_blue=209@c4_materials.wdl:705; specular_red=74@c4_materials.wdl:706; specular_green=65@c4_materials.wdl:707
    // specular_blue=62@c4_materials.wdl:708; emissive_red=15@c4_materials.wdl:709; emissive_green=16@c4_materials.wdl:710; emissive_blue=16@c4_materials.wdl:711
    // power=0@c4_materials.wdl:712; albedo=7@c4_materials.wdl:713; skill1=pixel_for_vec(vector(0,0,0),6.585,8888)@c4_materials.wdl:718
    {"Mtl_bayrak2", {}, 0x00000u, {116.0f, 106.0f, 103.0f}, {244.0f, 232.0f, 209.0f}, {74.0f, 65.0f, 62.0f}, {15.0f, 16.0f, 16.0f}, 0xfffu, 0.0f, 7.0f, true, true, 0u, false, 6.5849609375f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=228@c4_materials.wdl:1421; ambient_green=228@c4_materials.wdl:1422; ambient_blue=228@c4_materials.wdl:1423; diffuse_red=255@c4_materials.wdl:1424
    // diffuse_green=255@c4_materials.wdl:1425; diffuse_blue=255@c4_materials.wdl:1426; specular_red=0@c4_materials.wdl:1427; specular_green=0@c4_materials.wdl:1428
    // specular_blue=0@c4_materials.wdl:1429; emissive_red=7@c4_materials.wdl:1430; emissive_green=10@c4_materials.wdl:1431; emissive_blue=12@c4_materials.wdl:1432
    // power=0@c4_materials.wdl:1433; albedo=-96@c4_materials.wdl:1434; skill1=pixel_for_vec(vector(0,0,0),19.15,8888)@c4_materials.wdl:1439
    {"Mtl_c4_ayna_22", {}, 0x00000u, {228.0f, 228.0f, 228.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {7.0f, 10.0f, 12.0f}, 0xfffu, 0.0f, -96.0f, true, true, 0u, false, 19.1494140625f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=32@c4_materials.wdl:5113; ambient_green=50@c4_materials.wdl:5114; ambient_blue=54@c4_materials.wdl:5115; diffuse_red=244@c4_materials.wdl:5116
    // diffuse_green=247@c4_materials.wdl:5117; diffuse_blue=253@c4_materials.wdl:5118; specular_red=103@c4_materials.wdl:5119; specular_green=105@c4_materials.wdl:5120
    // specular_blue=103@c4_materials.wdl:5121; emissive_red=77@c4_materials.wdl:5122; emissive_green=47@c4_materials.wdl:5123; emissive_blue=53@c4_materials.wdl:5124
    // power=0@c4_materials.wdl:5125; albedo=34@c4_materials.wdl:5126; skill1=pixel_for_vec(vector(0,0,0),19.75,8888)@c4_materials.wdl:5131
    {"Mtl_c_ball", {}, 0x00000u, {32.0f, 50.0f, 54.0f}, {244.0f, 247.0f, 253.0f}, {103.0f, 105.0f, 103.0f}, {77.0f, 47.0f, 53.0f}, 0xfffu, 0.0f, 34.0f, true, true, 0u, false, 19.75f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:5575; ambient_green=145@c4_materials.wdl:5576; ambient_blue=145@c4_materials.wdl:5577; diffuse_red=183@c4_materials.wdl:5578
    // diffuse_green=180@c4_materials.wdl:5579; diffuse_blue=180@c4_materials.wdl:5580; specular_red=20@c4_materials.wdl:5581; specular_green=18@c4_materials.wdl:5582
    // specular_blue=18@c4_materials.wdl:5583; emissive_red=8@c4_materials.wdl:5584; emissive_green=8@c4_materials.wdl:5585; emissive_blue=8@c4_materials.wdl:5586
    // power=10@c4_materials.wdl:5587; albedo=41@c4_materials.wdl:5588; skill1=pixel_for_vec(vector(0,0,0),14.2,8888)@c4_materials.wdl:5593
    {"Mtl_car3", {}, 0x00000u, {145.0f, 145.0f, 145.0f}, {183.0f, 180.0f, 180.0f}, {20.0f, 18.0f, 18.0f}, {8.0f, 8.0f, 8.0f}, 0xfffu, 10.0f, 41.0f, true, true, 0u, false, 14.19921875f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=0@c4_materials.wdl:784; ambient_green=29@c4_materials.wdl:785; ambient_blue=30@c4_materials.wdl:786; diffuse_red=241@c4_materials.wdl:787
    // diffuse_green=244@c4_materials.wdl:788; diffuse_blue=241@c4_materials.wdl:789; specular_red=0@c4_materials.wdl:790; specular_green=0@c4_materials.wdl:791
    // specular_blue=0@c4_materials.wdl:792; emissive_red=36@c4_materials.wdl:793; emissive_green=30@c4_materials.wdl:794; emissive_blue=22@c4_materials.wdl:795
    // power=0@c4_materials.wdl:796; albedo=18@c4_materials.wdl:797; skill1=pixel_for_vec(vector(0,0,0),40.10,8888)@c4_materials.wdl:802
    {"Mtl_ghost", {}, 0x00000u, {0.0f, 29.0f, 30.0f}, {241.0f, 244.0f, 241.0f}, {0.0f, 0.0f, 0.0f}, {36.0f, 30.0f, 22.0f}, 0xfffu, 0.0f, 18.0f, true, true, 0u, false, 40.099609375f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:5870; ambient_green=135@c4_materials.wdl:5871; ambient_blue=135@c4_materials.wdl:5872; diffuse_red=53@c4_materials.wdl:5873
    // diffuse_green=53@c4_materials.wdl:5874; diffuse_blue=53@c4_materials.wdl:5875; specular_red=59@c4_materials.wdl:5876; specular_green=59@c4_materials.wdl:5877
    // specular_blue=59@c4_materials.wdl:5878; emissive_red=0@c4_materials.wdl:5879; emissive_green=0@c4_materials.wdl:5880; emissive_blue=0@c4_materials.wdl:5881
    // power=0@c4_materials.wdl:5882; albedo=50@c4_materials.wdl:5883; skill1=pixel_for_vec(vector(0,0,0),15.53,8888)@c4_materials.wdl:5888
    {"Mtl_gise_reklam", {}, 0x00000u, {145.0f, 135.0f, 135.0f}, {53.0f, 53.0f, 53.0f}, {59.0f, 59.0f, 59.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 15.529296875f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:2445; ambient_green=230@c4_materials.wdl:2446; ambient_blue=215@c4_materials.wdl:2447; diffuse_red=255@c4_materials.wdl:2448
    // diffuse_green=232@c4_materials.wdl:2449; diffuse_blue=181@c4_materials.wdl:2450; specular_red=0@c4_materials.wdl:2451; specular_green=0@c4_materials.wdl:2452
    // specular_blue=0@c4_materials.wdl:2453; emissive_red=27@c4_materials.wdl:2454; emissive_green=21@c4_materials.wdl:2455; emissive_blue=0@c4_materials.wdl:2456
    // power=0@c4_materials.wdl:2457; albedo=88@c4_materials.wdl:2458; skill1=pixel_for_vec(vector(0,0,0),24.54,8888)@c4_materials.wdl:2463
    {"Mtl_hell", {}, 0x00000u, {255.0f, 230.0f, 215.0f}, {255.0f, 232.0f, 181.0f}, {0.0f, 0.0f, 0.0f}, {27.0f, 21.0f, 0.0f}, 0xfffu, 0.0f, 88.0f, true, true, 0u, false, 24.5390625f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=206@c4_materials.wdl:1509; ambient_green=218@c4_materials.wdl:1510; ambient_blue=213@c4_materials.wdl:1511; diffuse_red=59@c4_materials.wdl:1512
    // diffuse_green=59@c4_materials.wdl:1513; diffuse_blue=59@c4_materials.wdl:1514; specular_red=0@c4_materials.wdl:1515; specular_green=0@c4_materials.wdl:1516
    // specular_blue=0@c4_materials.wdl:1517; emissive_red=24@c4_materials.wdl:1518; emissive_green=21@c4_materials.wdl:1519; emissive_blue=15@c4_materials.wdl:1520
    // power=0@c4_materials.wdl:1521; albedo=50@c4_materials.wdl:1522; skill1=pixel_for_vec(vector(0,0,0),2.395,8888)@c4_materials.wdl:1527
    {"Mtl_ic_panel", {}, 0x00000u, {206.0f, 218.0f, 213.0f}, {59.0f, 59.0f, 59.0f}, {0.0f, 0.0f, 0.0f}, {24.0f, 21.0f, 15.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 2.39453125f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=206@c4_materials.wdl:1550; ambient_green=218@c4_materials.wdl:1551; ambient_blue=213@c4_materials.wdl:1552; diffuse_red=59@c4_materials.wdl:1553
    // diffuse_green=59@c4_materials.wdl:1554; diffuse_blue=59@c4_materials.wdl:1555; specular_red=0@c4_materials.wdl:1556; specular_green=0@c4_materials.wdl:1557
    // specular_blue=0@c4_materials.wdl:1558; emissive_red=24@c4_materials.wdl:1559; emissive_green=21@c4_materials.wdl:1560; emissive_blue=15@c4_materials.wdl:1561
    // power=0@c4_materials.wdl:1562; albedo=50@c4_materials.wdl:1563; skill1=pixel_for_vec(vector(0,0,0),2.395,8888)@c4_materials.wdl:1568
    {"Mtl_ic_tavan", {}, 0x00000u, {206.0f, 218.0f, 213.0f}, {59.0f, 59.0f, 59.0f}, {0.0f, 0.0f, 0.0f}, {24.0f, 21.0f, 15.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 2.39453125f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:6490; ambient_green=255@c4_materials.wdl:6491; ambient_blue=255@c4_materials.wdl:6492; diffuse_red=255@c4_materials.wdl:6493
    // diffuse_green=255@c4_materials.wdl:6494; diffuse_blue=255@c4_materials.wdl:6495; specular_red=111@c4_materials.wdl:6496; specular_green=111@c4_materials.wdl:6497
    // specular_blue=114@c4_materials.wdl:6498; emissive_red=18@c4_materials.wdl:6499; emissive_green=19@c4_materials.wdl:6500; emissive_blue=18@c4_materials.wdl:6501
    // power=9@c4_materials.wdl:6502; albedo=2@c4_materials.wdl:6503; skill1=pixel_for_vec(vector(0,0,0),8.381,8888)@c4_materials.wdl:6508
    {"Mtl_kule_tut", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {111.0f, 111.0f, 114.0f}, {18.0f, 19.0f, 18.0f}, 0xfffu, 9.0f, 2.0f, true, true, 0u, false, 8.380859375f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=149@c4_materials.wdl:5909; ambient_green=152@c4_materials.wdl:5910; ambient_blue=158@c4_materials.wdl:5911; diffuse_red=164@c4_materials.wdl:5912
    // diffuse_green=164@c4_materials.wdl:5913; diffuse_blue=164@c4_materials.wdl:5914; specular_red=0@c4_materials.wdl:5915; specular_green=0@c4_materials.wdl:5916
    // specular_blue=0@c4_materials.wdl:5917; emissive_red=0@c4_materials.wdl:5918; emissive_green=0@c4_materials.wdl:5919; emissive_blue=0@c4_materials.wdl:5920
    // power=0@c4_materials.wdl:5921; albedo=50@c4_materials.wdl:5922; skill1=pixel_for_vec(vector(0,0,0),32.32,8888)@c4_materials.wdl:5927
    {"Mtl_kulube_cam", {}, 0x00000u, {149.0f, 152.0f, 158.0f}, {164.0f, 164.0f, 164.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 32.3193359375f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=129@c4_materials.wdl:739; ambient_green=117@c4_materials.wdl:740; ambient_blue=111@c4_materials.wdl:741; diffuse_red=255@c4_materials.wdl:742
    // diffuse_green=255@c4_materials.wdl:743; diffuse_blue=255@c4_materials.wdl:744; specular_red=12@c4_materials.wdl:745; specular_green=3@c4_materials.wdl:746
    // specular_blue=0@c4_materials.wdl:747; emissive_red=0@c4_materials.wdl:748; emissive_green=0@c4_materials.wdl:749; emissive_blue=0@c4_materials.wdl:750
    // power=3@c4_materials.wdl:751; albedo=34@c4_materials.wdl:752; skill1=pixel_for_vec(vector(0,0,0),18.55,8888)@c4_materials.wdl:757
    {"Mtl_loop_reklam", {}, 0x00000u, {129.0f, 117.0f, 111.0f}, {255.0f, 255.0f, 255.0f}, {12.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 3.0f, 34.0f, true, true, 0u, false, 18.5498046875f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=177@c4_materials.wdl:5623; ambient_green=177@c4_materials.wdl:5624; ambient_blue=177@c4_materials.wdl:5625; diffuse_red=255@c4_materials.wdl:5626
    // diffuse_green=255@c4_materials.wdl:5627; diffuse_blue=255@c4_materials.wdl:5628; specular_red=27@c4_materials.wdl:5629; specular_green=27@c4_materials.wdl:5630
    // specular_blue=24@c4_materials.wdl:5631; emissive_red=3@c4_materials.wdl:5632; emissive_green=0@c4_materials.wdl:5633; emissive_blue=0@c4_materials.wdl:5634
    // power=1@c4_materials.wdl:5635; albedo=41@c4_materials.wdl:5636; skill1=pixel_for_vec(vector(0,0,0),7.184,8888)@c4_materials.wdl:5641
    {"Mtl_robot_cs2", {}, 0x00000u, {177.0f, 177.0f, 177.0f}, {255.0f, 255.0f, 255.0f}, {27.0f, 27.0f, 24.0f}, {3.0f, 0.0f, 0.0f}, 0xfffu, 1.0f, 41.0f, true, true, 0u, false, 7.18359375f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:7066; ambient_green=140@c4_materials.wdl:7067; ambient_blue=140@c4_materials.wdl:7068; diffuse_red=190@c4_materials.wdl:7069
    // diffuse_green=190@c4_materials.wdl:7070; diffuse_blue=190@c4_materials.wdl:7071; specular_red=20@c4_materials.wdl:7072; specular_green=18@c4_materials.wdl:7073
    // specular_blue=18@c4_materials.wdl:7074; emissive_red=18@c4_materials.wdl:7075; emissive_green=18@c4_materials.wdl:7076; emissive_blue=18@c4_materials.wdl:7077
    // power=10@c4_materials.wdl:7078; albedo=41@c4_materials.wdl:7079; skill1=pixel_for_vec(vector(0,0,0),14.2,8888)@c4_materials.wdl:7084
    {"Mtl_robot_kaporta", {}, 0x00000u, {140.0f, 140.0f, 140.0f}, {190.0f, 190.0f, 190.0f}, {20.0f, 18.0f, 18.0f}, {18.0f, 18.0f, 18.0f}, 0xfffu, 10.0f, 41.0f, true, true, 0u, false, 14.19921875f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:6449; ambient_green=255@c4_materials.wdl:6450; ambient_blue=255@c4_materials.wdl:6451; diffuse_red=255@c4_materials.wdl:6452
    // diffuse_green=255@c4_materials.wdl:6453; diffuse_blue=255@c4_materials.wdl:6454; specular_red=111@c4_materials.wdl:6455; specular_green=111@c4_materials.wdl:6456
    // specular_blue=114@c4_materials.wdl:6457; emissive_red=18@c4_materials.wdl:6458; emissive_green=19@c4_materials.wdl:6459; emissive_blue=18@c4_materials.wdl:6460
    // power=9@c4_materials.wdl:6461; albedo=2@c4_materials.wdl:6462; skill1=pixel_for_vec(vector(0,0,0),8.381,8888)@c4_materials.wdl:6467
    {"Mtl_silindir", {}, 0x00000u, {255.0f, 255.0f, 255.0f}, {255.0f, 255.0f, 255.0f}, {111.0f, 111.0f, 114.0f}, {18.0f, 19.0f, 18.0f}, 0xfffu, 9.0f, 2.0f, true, true, 0u, false, 8.380859375f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=253@c4_materials.wdl:1350; ambient_green=56@c4_materials.wdl:1351; ambient_blue=164@c4_materials.wdl:1352; diffuse_red=255@c4_materials.wdl:1353
    // diffuse_green=254@c4_materials.wdl:1354; diffuse_blue=250@c4_materials.wdl:1355; specular_red=0@c4_materials.wdl:1356; specular_green=1@c4_materials.wdl:1357
    // specular_blue=0@c4_materials.wdl:1358; emissive_red=41@c4_materials.wdl:1359; emissive_green=56@c4_materials.wdl:1360; emissive_blue=22@c4_materials.wdl:1361
    // power=0@c4_materials.wdl:1362; albedo=104@c4_materials.wdl:1363; skill1=pixel_for_vec(vector(0,0,0),14.36,8888)@c4_materials.wdl:1368
    {"Mtl_spooot", {}, 0x00000u, {253.0f, 56.0f, 164.0f}, {255.0f, 254.0f, 250.0f}, {0.0f, 1.0f, 0.0f}, {41.0f, 56.0f, 22.0f}, 0xfffu, 0.0f, 104.0f, true, true, 0u, false, 14.359375f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=141@c4_materials.wdl:1206; ambient_green=244@c4_materials.wdl:1207; ambient_blue=244@c4_materials.wdl:1208; diffuse_red=203@c4_materials.wdl:1209
    // diffuse_green=199@c4_materials.wdl:1210; diffuse_blue=196@c4_materials.wdl:1211; specular_red=247@c4_materials.wdl:1212; specular_green=244@c4_materials.wdl:1213
    // specular_blue=247@c4_materials.wdl:1214; emissive_red=137@c4_materials.wdl:1215; emissive_green=155@c4_materials.wdl:1216; emissive_blue=155@c4_materials.wdl:1217
    // power=4@c4_materials.wdl:1218; albedo=20@c4_materials.wdl:1219; skill1=pixel_for_vec(vector(0,0,0),10.77,8888)@c4_materials.wdl:1224
    {"Mtl_tentem", {}, 0x00000u, {141.0f, 244.0f, 244.0f}, {203.0f, 199.0f, 196.0f}, {247.0f, 244.0f, 247.0f}, {137.0f, 155.0f, 155.0f}, 0xfffu, 4.0f, 20.0f, true, true, 0u, false, 10.76953125f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=143@c4_materials.wdl:821; ambient_green=146@c4_materials.wdl:822; ambient_blue=149@c4_materials.wdl:823; diffuse_red=255@c4_materials.wdl:824
    // diffuse_green=255@c4_materials.wdl:825; diffuse_blue=255@c4_materials.wdl:826; specular_red=29@c4_materials.wdl:827; specular_green=30@c4_materials.wdl:828
    // specular_blue=24@c4_materials.wdl:829; emissive_red=45@c4_materials.wdl:830; emissive_green=38@c4_materials.wdl:831; emissive_blue=25@c4_materials.wdl:832
    // power=0@c4_materials.wdl:833; albedo=50@c4_materials.wdl:834; skill1=pixel_for_vec(vector(0,0,0),12.57,8888)@c4_materials.wdl:839
    {"Mtl_vinc_mod", {}, 0x00000u, {143.0f, 146.0f, 149.0f}, {255.0f, 255.0f, 255.0f}, {29.0f, 30.0f, 24.0f}, {45.0f, 38.0f, 25.0f}, 0xfffu, 0.0f, 50.0f, true, true, 0u, false, 12.5693359375f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=114@c4_materials.wdl:1245; ambient_green=70@c4_materials.wdl:1246; ambient_blue=58@c4_materials.wdl:1247; diffuse_red=255@c4_materials.wdl:1248
    // diffuse_green=255@c4_materials.wdl:1249; diffuse_blue=255@c4_materials.wdl:1250; specular_red=0@c4_materials.wdl:1251; specular_green=0@c4_materials.wdl:1252
    // specular_blue=0@c4_materials.wdl:1253; emissive_red=0@c4_materials.wdl:1254; emissive_green=0@c4_materials.wdl:1255; emissive_blue=0@c4_materials.wdl:1256
    // power=0@c4_materials.wdl:1257; albedo=-65@c4_materials.wdl:1258; skill1=pixel_for_vec(vector(0,0,0),5.986,8888)@c4_materials.wdl:1263
    {"Mtl_yeni_atmos_sag", {}, 0x00000u, {114.0f, 70.0f, 58.0f}, {255.0f, 255.0f, 255.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0xfffu, 0.0f, -65.0f, true, true, 0u, false, 5.9853515625f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=67@c4_materials.wdl:1291; ambient_green=54@c4_materials.wdl:1292; ambient_blue=50@c4_materials.wdl:1293; diffuse_red=255@c4_materials.wdl:1294
    // diffuse_green=251@c4_materials.wdl:1295; diffuse_blue=251@c4_materials.wdl:1296; specular_red=9@c4_materials.wdl:1297; specular_green=0@c4_materials.wdl:1298
    // specular_blue=0@c4_materials.wdl:1299; emissive_red=22@c4_materials.wdl:1300; emissive_green=6@c4_materials.wdl:1301; emissive_blue=4@c4_materials.wdl:1302
    // power=0@c4_materials.wdl:1303; albedo=-53@c4_materials.wdl:1304; skill1=pixel_for_vec(vector(0,0,0),4.190,8888)@c4_materials.wdl:1309
    {"Mtl_yeni_atmos_sol", {}, 0x00000u, {67.0f, 54.0f, 50.0f}, {255.0f, 251.0f, 251.0f}, {9.0f, 0.0f, 0.0f}, {22.0f, 6.0f, 4.0f}, 0xfffu, 0.0f, -53.0f, true, true, 0u, false, 4.189453125f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=180@c4_materials.wdl:1020; ambient_green=175@c4_materials.wdl:1021; ambient_blue=170@c4_materials.wdl:1022; diffuse_red=190@c4_materials.wdl:1023
    // diffuse_green=190@c4_materials.wdl:1024; diffuse_blue=190@c4_materials.wdl:1025; specular_red=30@c4_materials.wdl:1026; specular_green=30@c4_materials.wdl:1027
    // specular_blue=30@c4_materials.wdl:1028; emissive_red=8@c4_materials.wdl:1029; emissive_green=8@c4_materials.wdl:1030; emissive_blue=8@c4_materials.wdl:1031
    // power=10@c4_materials.wdl:1032; albedo=41@c4_materials.wdl:1033; skill1=pixel_for_vec(vector(0,0,0),14.2,8888)@c4_materials.wdl:1038
    {"Mtl_kul_car", {}, 0x00000u, {180.0f, 175.0f, 170.0f}, {190.0f, 190.0f, 190.0f}, {30.0f, 30.0f, 30.0f}, {8.0f, 8.0f, 8.0f}, 0xfffu, 10.0f, 41.0f, true, true, 0u, false, 14.19921875f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:1067; ambient_green=140@c4_materials.wdl:1068; ambient_blue=140@c4_materials.wdl:1069; diffuse_red=200@c4_materials.wdl:1070
    // diffuse_green=200@c4_materials.wdl:1071; diffuse_blue=200@c4_materials.wdl:1072; specular_red=30@c4_materials.wdl:1073; specular_green=30@c4_materials.wdl:1074
    // specular_blue=30@c4_materials.wdl:1075; emissive_red=10@c4_materials.wdl:1076; emissive_green=10@c4_materials.wdl:1077; emissive_blue=10@c4_materials.wdl:1078
    // power=0@c4_materials.wdl:1079; albedo=15@c4_materials.wdl:1080; skill1=pixel_for_vec(vector(0,0,0),19.2,8888)@c4_materials.wdl:1085
    {"Mtl_kul_robot", {}, 0x00000u, {140.0f, 140.0f, 140.0f}, {200.0f, 200.0f, 200.0f}, {30.0f, 30.0f, 30.0f}, {10.0f, 10.0f, 10.0f}, 0xfffu, 0.0f, 15.0f, true, true, 0u, false, 19.19921875f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=239@c4_materials.wdl:1114; ambient_green=238@c4_materials.wdl:1115; ambient_blue=235@c4_materials.wdl:1116; diffuse_red=241@c4_materials.wdl:1117
    // diffuse_green=245@c4_materials.wdl:1118; diffuse_blue=242@c4_materials.wdl:1119; specular_red=30@c4_materials.wdl:1120; specular_green=30@c4_materials.wdl:1121
    // specular_blue=30@c4_materials.wdl:1122; emissive_red=4@c4_materials.wdl:1123; emissive_green=4@c4_materials.wdl:1124; emissive_blue=0@c4_materials.wdl:1125
    // power=1@c4_materials.wdl:1126; albedo=104@c4_materials.wdl:1127; skill1=pixel_for_vec(vector(0,0,0),19.33,8888)@c4_materials.wdl:1132
    {"Mtl_siyah_car", {}, 0x00000u, {239.0f, 238.0f, 235.0f}, {241.0f, 245.0f, 242.0f}, {30.0f, 30.0f, 30.0f}, {4.0f, 4.0f, 0.0f}, 0xfffu, 1.0f, 104.0f, true, true, 0u, false, 19.3291015625f, true},
    // Representative source profile ID 2; payload shared where selected fields are identical.
    // ambient_red=183@c4_materials.wdl:1161; ambient_green=188@c4_materials.wdl:1162; ambient_blue=185@c4_materials.wdl:1163; diffuse_red=290@c4_materials.wdl:1164
    // diffuse_green=290@c4_materials.wdl:1165; diffuse_blue=290@c4_materials.wdl:1166; specular_red=30@c4_materials.wdl:1167; specular_green=30@c4_materials.wdl:1168
    // specular_blue=30@c4_materials.wdl:1169; emissive_red=15@c4_materials.wdl:1170; emissive_green=15@c4_materials.wdl:1171; emissive_blue=15@c4_materials.wdl:1172
    // power=0@c4_materials.wdl:1173; albedo=15@c4_materials.wdl:1174; skill1=pixel_for_vec(vector(0,0,0),28.53,8888)@c4_materials.wdl:1179
    {"Mtl_siyah_robot", {}, 0x00000u, {183.0f, 188.0f, 185.0f}, {290.0f, 290.0f, 290.0f}, {30.0f, 30.0f, 30.0f}, {15.0f, 15.0f, 15.0f}, 0xfffu, 0.0f, 15.0f, true, true, 0u, false, 28.529296875f, true},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=190@c4_materials.wdl:6940; ambient_green=190@c4_materials.wdl:6941; ambient_blue=190@c4_materials.wdl:6942; skill1=float(1.300)@c4_materials.wdl:6944
    // skill2=float(0.100)@c4_materials.wdl:6945; skill3=float(1.000)@c4_materials.wdl:6946; skill4=float(1.000)@c4_materials.wdl:6947; skill5=float(-161.0)@c4_materials.wdl:6948
    // skill6=float(-200.0)@c4_materials.wdl:6949; skill7=float(-109.0)@c4_materials.wdl:6950
    {"Mtl_araba_lastik", {1.2998046875f, 0.099609375f, 1.0f, 1.0f, -161.0f, -200.0f, -109.0f}, 0x0007fu, {190.0f, 190.0f, 190.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=60@c4_materials.wdl:6995; ambient_green=60@c4_materials.wdl:6996; ambient_blue=60@c4_materials.wdl:6997; skill1=float(1.861)@c4_materials.wdl:6999
    // skill2=float(0.766)@c4_materials.wdl:7000; skill3=float(0.670)@c4_materials.wdl:7001; skill4=float(2.155)@c4_materials.wdl:7002; skill5=float(-381.)@c4_materials.wdl:7003
    // skill6=float(-601.)@c4_materials.wdl:7004; skill7=float(-730.)@c4_materials.wdl:7005
    {"Mtl_arac_fargovde", {1.8603515625f, 0.765625f, 0.669921875f, 2.154296875f, -381.0f, -601.0f, -730.0f}, 0x0007fu, {60.0f, 60.0f, 60.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=200@c4_materials.wdl:2536; ambient_green=219@c4_materials.wdl:2537; ambient_blue=209@c4_materials.wdl:2538; skill1=float(1.100)@c4_materials.wdl:2540
    // skill2=float(1)@c4_materials.wdl:2541; skill3=float(0)@c4_materials.wdl:2542; skill4=float(8)@c4_materials.wdl:2543; skill5=float(-440)@c4_materials.wdl:2544
    // skill6=float(-440)@c4_materials.wdl:2545; skill7=float(-440)@c4_materials.wdl:2546
    {"Mtl_arac_koltuk", {1.099609375f, 1.0f, 0.0f, 8.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {200.0f, 219.0f, 209.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=134@c4_materials.wdl:5599; ambient_green=131@c4_materials.wdl:5600; ambient_blue=131@c4_materials.wdl:5601; skill1=float(1.148)@c4_materials.wdl:5603
    // skill2=float(0.574)@c4_materials.wdl:5604; skill3=float(0.670)@c4_materials.wdl:5605; skill4=float(2.155)@c4_materials.wdl:5606; skill5=float(-58.2)@c4_materials.wdl:5607
    // skill6=float(-588.)@c4_materials.wdl:5608; skill7=float(-407.)@c4_materials.wdl:5609
    {"Mtl_car3", {1.1474609375f, 0.5732421875f, 0.669921875f, 2.154296875f, -58.19921875f, -588.0f, -407.0f}, 0x0007fu, {134.0f, 131.0f, 131.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=247@c4_materials.wdl:1534; ambient_green=255@c4_materials.wdl:1535; ambient_blue=244@c4_materials.wdl:1536; skill1=float(1.053)@c4_materials.wdl:1538
    // skill2=float(0.096)@c4_materials.wdl:1539; skill3=float(0.670)@c4_materials.wdl:1540; skill4=float(10.77)@c4_materials.wdl:1541; skill5=float(-549.)@c4_materials.wdl:1542
    // skill6=float(-135.)@c4_materials.wdl:1543; skill7=float(-472.)@c4_materials.wdl:1544
    {"Mtl_ic_panel", {1.052734375f, 0.095703125f, 0.669921875f, 10.76953125f, -549.0f, -135.0f, -472.0f}, 0x0007fu, {247.0f, 255.0f, 244.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=235@c4_materials.wdl:1478; ambient_green=235@c4_materials.wdl:1479; ambient_blue=227@c4_materials.wdl:1480; skill1=float(0.861)@c4_materials.wdl:1482
    // skill2=float(0.287)@c4_materials.wdl:1483; skill3=float(0.479)@c4_materials.wdl:1484; skill4=float(2.155)@c4_materials.wdl:1485; skill5=float(-472.)@c4_materials.wdl:1486
    // skill6=float(-704.)@c4_materials.wdl:1487; skill7=float(-860.)@c4_materials.wdl:1488
    {"Mtl_ic_simit", {0.8603515625f, 0.2861328125f, 0.478515625f, 2.154296875f, -472.0f, -704.0f, -860.0f}, 0x0007fu, {235.0f, 235.0f, 227.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=247@c4_materials.wdl:1575; ambient_green=255@c4_materials.wdl:1576; ambient_blue=244@c4_materials.wdl:1577; skill1=float(1.053)@c4_materials.wdl:1579
    // skill2=float(0.096)@c4_materials.wdl:1580; skill3=float(0.670)@c4_materials.wdl:1581; skill4=float(10.77)@c4_materials.wdl:1582; skill5=float(-549.)@c4_materials.wdl:1583
    // skill6=float(-135.)@c4_materials.wdl:1584; skill7=float(-472.)@c4_materials.wdl:1585
    {"Mtl_ic_tavan", {1.052734375f, 0.095703125f, 0.669921875f, 10.76953125f, -549.0f, -135.0f, -472.0f}, 0x0007fu, {247.0f, 255.0f, 244.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:5647; ambient_green=134@c4_materials.wdl:5648; ambient_blue=130@c4_materials.wdl:5649; skill1=float(1.450)@c4_materials.wdl:5651
    // skill2=float(0.800)@c4_materials.wdl:5652; skill3=float(0.700)@c4_materials.wdl:5653; skill4=float(1.055)@c4_materials.wdl:5654; skill5=float(-58.2)@c4_materials.wdl:5655
    // skill6=float(-514.)@c4_materials.wdl:5656; skill7=float(-481.)@c4_materials.wdl:5657
    {"Mtl_robot_cs2", {1.44921875f, 0.7998046875f, 0.69921875f, 1.0546875f, -58.19921875f, -514.0f, -481.0f}, 0x0007fu, {145.0f, 134.0f, 130.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=225@c4_materials.wdl:7028; ambient_green=225@c4_materials.wdl:7029; ambient_blue=138@c4_materials.wdl:7030; skill1=float(0.287)@c4_materials.wdl:7032
    // skill2=float(0.383)@c4_materials.wdl:7033; skill3=float(0)@c4_materials.wdl:7034; skill4=float(8)@c4_materials.wdl:7035; skill5=float(472.0)@c4_materials.wdl:7036
    // skill6=float(-291.)@c4_materials.wdl:7037; skill7=float(627.2)@c4_materials.wdl:7038
    {"Mtl_robot_fargovde", {0.2861328125f, 0.3828125f, 0.0f, 8.0f, 472.0f, -291.0f, 627.19921875f}, 0x0007fu, {225.0f, 225.0f, 138.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=134@c4_materials.wdl:7090; ambient_green=131@c4_materials.wdl:7091; ambient_blue=131@c4_materials.wdl:7092; skill1=float(1.148)@c4_materials.wdl:7094
    // skill2=float(0.574)@c4_materials.wdl:7095; skill3=float(0.670)@c4_materials.wdl:7096; skill4=float(2.155)@c4_materials.wdl:7097; skill5=float(-58.2)@c4_materials.wdl:7098
    // skill6=float(-588.)@c4_materials.wdl:7099; skill7=float(-407.)@c4_materials.wdl:7100
    {"Mtl_robot_kaporta", {1.1474609375f, 0.5732421875f, 0.669921875f, 2.154296875f, -58.19921875f, -588.0f, -407.0f}, 0x0007fu, {134.0f, 131.0f, 131.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=137@c4_materials.wdl:1045; ambient_green=131@c4_materials.wdl:1046; ambient_blue=128@c4_materials.wdl:1047; skill1=float(1.244)@c4_materials.wdl:1049
    // skill2=float(0.670)@c4_materials.wdl:1050; skill3=float(0.861)@c4_materials.wdl:1051; skill4=float(2.155)@c4_materials.wdl:1052; skill5=float(-58.2)@c4_materials.wdl:1053
    // skill6=float(-588.)@c4_materials.wdl:1054; skill7=float(-407.)@c4_materials.wdl:1055
    {"Mtl_kul_car", {1.2431640625f, 0.669921875f, 0.8603515625f, 2.154296875f, -58.19921875f, -588.0f, -407.0f}, 0x0007fu, {137.0f, 131.0f, 128.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=137@c4_materials.wdl:1092; ambient_green=131@c4_materials.wdl:1093; ambient_blue=128@c4_materials.wdl:1094; skill1=float(1.244)@c4_materials.wdl:1096
    // skill2=float(0.670)@c4_materials.wdl:1097; skill3=float(0.861)@c4_materials.wdl:1098; skill4=float(2.155)@c4_materials.wdl:1099; skill5=float(-58.2)@c4_materials.wdl:1100
    // skill6=float(-588.)@c4_materials.wdl:1101; skill7=float(-407.)@c4_materials.wdl:1102
    {"Mtl_kul_robot", {1.2431640625f, 0.669921875f, 0.8603515625f, 2.154296875f, -58.19921875f, -588.0f, -407.0f}, 0x0007fu, {137.0f, 131.0f, 128.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=216@c4_materials.wdl:1139; ambient_green=210@c4_materials.wdl:1140; ambient_blue=192@c4_materials.wdl:1141; skill1=float(1.318)@c4_materials.wdl:1143
    // skill2=float(0.574)@c4_materials.wdl:1144; skill3=float(1.531)@c4_materials.wdl:1145; skill4=float(2.155)@c4_materials.wdl:1146; skill5=float(-58.2)@c4_materials.wdl:1147
    // skill6=float(-588.)@c4_materials.wdl:1148; skill7=float(-407.0)@c4_materials.wdl:1149
    {"Mtl_siyah_car", {1.3173828125f, 0.5732421875f, 1.5302734375f, 2.154296875f, -58.19921875f, -588.0f, -407.0f}, 0x0007fu, {216.0f, 210.0f, 192.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 12; payload shared where selected fields are identical.
    // ambient_red=216@c4_materials.wdl:1186; ambient_green=210@c4_materials.wdl:1187; ambient_blue=192@c4_materials.wdl:1188; skill1=float(1.531)@c4_materials.wdl:1190
    // skill2=float(0.861)@c4_materials.wdl:1191; skill3=float(1.244)@c4_materials.wdl:1192; skill4=float(2.155)@c4_materials.wdl:1193; skill5=float(-795.)@c4_materials.wdl:1194
    // skill6=float(-316.)@c4_materials.wdl:1195; skill7=float(19.35)@c4_materials.wdl:1196
    {"Mtl_siyah_robot", {1.5302734375f, 0.8603515625f, 1.2431640625f, 2.154296875f, -795.0f, -316.0f, 19.349609375f}, 0x0007fu, {216.0f, 210.0f, 192.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 14; payload shared where selected fields are identical.
    // ambient_red=134@c4_materials.wdl:5599; ambient_green=131@c4_materials.wdl:5600; ambient_blue=131@c4_materials.wdl:5601; skill1=float(1.148)@c4_materials.wdl:5603
    // skill2=float(0.574)@c4_materials.wdl:5604; skill3=float(0.670)@c4_materials.wdl:5605; skill4=float(2.155)@c4_materials.wdl:5606; skill5=float(-58.2)@c4_materials.wdl:5607
    // skill6=float(-588.)@c4_materials.wdl:5608; skill7=float(-407.)@c4_materials.wdl:5609; skill9=float(0.203)@c4_materials.wdl:5615
    {"Mtl_car3", {1.1474609375f, 0.5732421875f, 0.669921875f, 2.154296875f, -58.19921875f, -588.0f, -407.0f, 0.0f, 0.2021484375f}, 0x0017fu, {134.0f, 131.0f, 131.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 14; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:5647; ambient_green=134@c4_materials.wdl:5648; ambient_blue=130@c4_materials.wdl:5649; skill1=float(1.450)@c4_materials.wdl:5651
    // skill2=float(0.800)@c4_materials.wdl:5652; skill3=float(0.700)@c4_materials.wdl:5653; skill4=float(1.055)@c4_materials.wdl:5654; skill5=float(-58.2)@c4_materials.wdl:5655
    // skill6=float(-514.)@c4_materials.wdl:5656; skill7=float(-481.)@c4_materials.wdl:5657; skill9=float(0.070)@c4_materials.wdl:5663
    {"Mtl_robot_cs2", {1.44921875f, 0.7998046875f, 0.69921875f, 1.0546875f, -58.19921875f, -514.0f, -481.0f, 0.0f, 0.0693359375f}, 0x0017fu, {145.0f, 134.0f, 130.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 14; payload shared where selected fields are identical.
    // ambient_red=134@c4_materials.wdl:7090; ambient_green=131@c4_materials.wdl:7091; ambient_blue=131@c4_materials.wdl:7092; skill1=float(1.148)@c4_materials.wdl:7094
    // skill2=float(0.574)@c4_materials.wdl:7095; skill3=float(0.670)@c4_materials.wdl:7096; skill4=float(2.155)@c4_materials.wdl:7097; skill5=float(-58.2)@c4_materials.wdl:7098
    // skill6=float(-588.)@c4_materials.wdl:7099; skill7=float(-407.)@c4_materials.wdl:7100; skill9=float(0.203)@c4_materials.wdl:7106
    {"Mtl_robot_kaporta", {1.1474609375f, 0.5732421875f, 0.669921875f, 2.154296875f, -58.19921875f, -588.0f, -407.0f, 0.0f, 0.2021484375f}, 0x0017fu, {134.0f, 131.0f, 131.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 14; payload shared where selected fields are identical.
    // ambient_red=137@c4_materials.wdl:1045; ambient_green=131@c4_materials.wdl:1046; ambient_blue=128@c4_materials.wdl:1047; skill1=float(1.244)@c4_materials.wdl:1049
    // skill2=float(0.670)@c4_materials.wdl:1050; skill3=float(0.861)@c4_materials.wdl:1051; skill4=float(2.155)@c4_materials.wdl:1052; skill5=float(-58.2)@c4_materials.wdl:1053
    // skill6=float(-588.)@c4_materials.wdl:1054; skill7=float(-407.)@c4_materials.wdl:1055; skill9=float(0.203)@c4_materials.wdl:1060
    {"Mtl_kul_car", {1.2431640625f, 0.669921875f, 0.8603515625f, 2.154296875f, -58.19921875f, -588.0f, -407.0f, 0.0f, 0.2021484375f}, 0x0017fu, {137.0f, 131.0f, 128.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 14; payload shared where selected fields are identical.
    // ambient_red=137@c4_materials.wdl:1092; ambient_green=131@c4_materials.wdl:1093; ambient_blue=128@c4_materials.wdl:1094; skill1=float(1.244)@c4_materials.wdl:1096
    // skill2=float(0.670)@c4_materials.wdl:1097; skill3=float(0.861)@c4_materials.wdl:1098; skill4=float(2.155)@c4_materials.wdl:1099; skill5=float(-58.2)@c4_materials.wdl:1100
    // skill6=float(-588.)@c4_materials.wdl:1101; skill7=float(-407.)@c4_materials.wdl:1102; skill9=float(0.203)@c4_materials.wdl:1107
    {"Mtl_kul_robot", {1.2431640625f, 0.669921875f, 0.8603515625f, 2.154296875f, -58.19921875f, -588.0f, -407.0f, 0.0f, 0.2021484375f}, 0x0017fu, {137.0f, 131.0f, 128.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 14; payload shared where selected fields are identical.
    // ambient_red=216@c4_materials.wdl:1139; ambient_green=210@c4_materials.wdl:1140; ambient_blue=192@c4_materials.wdl:1141; skill1=float(1.318)@c4_materials.wdl:1143
    // skill2=float(0.574)@c4_materials.wdl:1144; skill3=float(1.531)@c4_materials.wdl:1145; skill4=float(2.155)@c4_materials.wdl:1146; skill5=float(-58.2)@c4_materials.wdl:1147
    // skill6=float(-588.)@c4_materials.wdl:1148; skill7=float(-407.0)@c4_materials.wdl:1149; skill9=float(0.197)@c4_materials.wdl:1154
    {"Mtl_siyah_car", {1.3173828125f, 0.5732421875f, 1.5302734375f, 2.154296875f, -58.19921875f, -588.0f, -407.0f, 0.0f, 0.1962890625f}, 0x0017fu, {216.0f, 210.0f, 192.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 14; payload shared where selected fields are identical.
    // ambient_red=216@c4_materials.wdl:1186; ambient_green=210@c4_materials.wdl:1187; ambient_blue=192@c4_materials.wdl:1188; skill1=float(1.531)@c4_materials.wdl:1190
    // skill2=float(0.861)@c4_materials.wdl:1191; skill3=float(1.244)@c4_materials.wdl:1192; skill4=float(2.155)@c4_materials.wdl:1193; skill5=float(-795.)@c4_materials.wdl:1194
    // skill6=float(-316.)@c4_materials.wdl:1195; skill7=float(19.35)@c4_materials.wdl:1196; skill9=float(0.129)@c4_materials.wdl:1201
    {"Mtl_siyah_robot", {1.5302734375f, 0.8603515625f, 1.2431640625f, 2.154296875f, -795.0f, -316.0f, 19.349609375f, 0.0f, 0.12890625f}, 0x0017fu, {216.0f, 210.0f, 192.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=172@c4_materials.wdl:5390; ambient_green=178@c4_materials.wdl:5391; ambient_blue=222@c4_materials.wdl:5392; skill1=float(1.340)@c4_materials.wdl:5394
    // skill2=float(1.914)@c4_materials.wdl:5395; skill3=float(1.340)@c4_materials.wdl:5396; skill4=float(15.08)@c4_materials.wdl:5397; skill5=float(122.8)@c4_materials.wdl:5398
    // skill6=float(-84.1)@c4_materials.wdl:5399; skill7=float(161.6)@c4_materials.wdl:5400
    {"Mtl_33", {1.33984375f, 1.9130859375f, 1.33984375f, 15.0791015625f, 122.7998046875f, -84.099609375f, 161.599609375f}, 0x0007fu, {172.0f, 178.0f, 222.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=164@c4_materials.wdl:3281; ambient_green=161@c4_materials.wdl:3282; ambient_blue=161@c4_materials.wdl:3283; skill1=float(0.957)@c4_materials.wdl:3285
    // skill2=float(0.287)@c4_materials.wdl:3286; skill3=float(0.096)@c4_materials.wdl:3287; skill4=float(8)@c4_materials.wdl:3288; skill5=float(-653.)@c4_materials.wdl:3289
    // skill6=float(-278.)@c4_materials.wdl:3290; skill7=float(-161.)@c4_materials.wdl:3291
    {"Mtl_adres", {0.9560546875f, 0.2861328125f, 0.095703125f, 8.0f, -653.0f, -278.0f, -161.0f}, 0x0007fu, {164.0f, 161.0f, 161.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:870; ambient_green=146@c4_materials.wdl:871; ambient_blue=172@c4_materials.wdl:872; skill1=float(2.297)@c4_materials.wdl:874
    // skill2=float(0)@c4_materials.wdl:875; skill3=float(0.574)@c4_materials.wdl:876; skill4=float(2.155)@c4_materials.wdl:877; skill5=float(-666.)@c4_materials.wdl:878
    // skill6=float(-407.)@c4_materials.wdl:879; skill7=float(174.5)@c4_materials.wdl:880
    {"Mtl_afis_site", {2.296875f, 0.0f, 0.5732421875f, 2.154296875f, -666.0f, -407.0f, 174.5f}, 0x0007fu, {255.0f, 146.0f, 172.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4344; ambient_green=236@c4_materials.wdl:4345; ambient_blue=241@c4_materials.wdl:4346; skill1=float(1.053)@c4_materials.wdl:4348
    // skill2=float(0.766)@c4_materials.wdl:4349; skill3=float(0.957)@c4_materials.wdl:4350; skill4=float(2.155)@c4_materials.wdl:4351; skill5=float(-614.)@c4_materials.wdl:4352
    // skill6=float(-342.)@c4_materials.wdl:4353; skill7=float(-440)@c4_materials.wdl:4354
    {"Mtl_agac", {1.052734375f, 0.765625f, 0.9560546875f, 2.154296875f, -614.0f, -342.0f, -440.0f}, 0x0007fu, {255.0f, 236.0f, 241.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=177@c4_materials.wdl:4246; ambient_green=177@c4_materials.wdl:4247; ambient_blue=186@c4_materials.wdl:4248; skill1=float(0.861)@c4_materials.wdl:4250
    // skill2=float(1.723)@c4_materials.wdl:4251; skill3=float(0.287)@c4_materials.wdl:4252; skill4=float(21.55)@c4_materials.wdl:4253; skill5=float(-536.)@c4_materials.wdl:4254
    // skill6=float(-368.)@c4_materials.wdl:4255; skill7=float(-446.)@c4_materials.wdl:4256
    {"Mtl_ara_yol", {0.8603515625f, 1.72265625f, 0.2861328125f, 21.5498046875f, -536.0f, -368.0f, -446.0f}, 0x0007fu, {177.0f, 177.0f, 186.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=67@c4_materials.wdl:910; ambient_green=61@c4_materials.wdl:911; ambient_blue=67@c4_materials.wdl:912; skill1=float(1.914)@c4_materials.wdl:914
    // skill2=float(0)@c4_materials.wdl:915; skill3=float(0.670)@c4_materials.wdl:916; skill4=float(2.155)@c4_materials.wdl:917; skill5=float(-808.)@c4_materials.wdl:918
    // skill6=float(-1080)@c4_materials.wdl:919; skill7=float(420.2)@c4_materials.wdl:920
    {"Mtl_askagel", {1.9130859375f, 0.0f, 0.669921875f, 2.154296875f, -808.0f, -1080.0f, 420.19921875f}, 0x0007fu, {67.0f, 61.0f, 67.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=155@c4_materials.wdl:1392; ambient_green=135@c4_materials.wdl:1393; ambient_blue=122@c4_materials.wdl:1394; skill1=float(1.053)@c4_materials.wdl:1396
    // skill2=float(0.096)@c4_materials.wdl:1397; skill3=float(0.096)@c4_materials.wdl:1398; skill4=float(2.155)@c4_materials.wdl:1399; skill5=float(1080)@c4_materials.wdl:1400
    // skill6=float(860.0)@c4_materials.wdl:1401; skill7=float(-1080)@c4_materials.wdl:1402
    {"Mtl_bal_kil", {1.052734375f, 0.095703125f, 0.095703125f, 2.154296875f, 1080.0f, 860.0f, -1080.0f}, 0x0007fu, {155.0f, 135.0f, 122.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=155@c4_materials.wdl:1004; ambient_green=135@c4_materials.wdl:1005; ambient_blue=122@c4_materials.wdl:1006; skill1=float(1.053)@c4_materials.wdl:1008
    // skill2=float(0.096)@c4_materials.wdl:1009; skill3=float(0.096)@c4_materials.wdl:1010; skill4=float(2.155)@c4_materials.wdl:1011; skill5=float(1080)@c4_materials.wdl:1012
    // skill6=float(860.0)@c4_materials.wdl:1013; skill7=float(-1080)@c4_materials.wdl:1014
    {"Mtl_bal_kopek", {1.052734375f, 0.095703125f, 0.095703125f, 2.154296875f, 1080.0f, 860.0f, -1080.0f}, 0x0007fu, {155.0f, 135.0f, 122.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:5155; ambient_green=255@c4_materials.wdl:5156; ambient_blue=254@c4_materials.wdl:5157; skill1=float(0.857)@c4_materials.wdl:5159
    // skill2=float(1.244)@c4_materials.wdl:5160; skill3=float(0.799)@c4_materials.wdl:5161; skill4=float(8)@c4_materials.wdl:5162; skill5=float(-472.)@c4_materials.wdl:5163
    // skill6=float(-446.)@c4_materials.wdl:5164; skill7=float(-459.)@c4_materials.wdl:5165
    {"Mtl_balon", {0.8564453125f, 1.2431640625f, 0.798828125f, 8.0f, -472.0f, -446.0f, -459.0f}, 0x0007fu, {255.0f, 255.0f, 254.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=190@c4_materials.wdl:6682; ambient_green=187@c4_materials.wdl:6683; ambient_blue=185@c4_materials.wdl:6684; skill1=float(0.966)@c4_materials.wdl:6686
    // skill2=float(0.818)@c4_materials.wdl:6687; skill3=float(0.574)@c4_materials.wdl:6688; skill4=float(2.155)@c4_materials.wdl:6689; skill5=float(-440)@c4_materials.wdl:6690
    // skill6=float(-440)@c4_materials.wdl:6691; skill7=float(-440)@c4_materials.wdl:6692
    {"Mtl_bariyer", {0.9658203125f, 0.8173828125f, 0.5732421875f, 2.154296875f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {190.0f, 187.0f, 185.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=190@c4_materials.wdl:6399; ambient_green=192@c4_materials.wdl:6400; ambient_blue=192@c4_materials.wdl:6401; skill1=float(1.244)@c4_materials.wdl:6403
    // skill2=float(0.861)@c4_materials.wdl:6404; skill3=float(0.957)@c4_materials.wdl:6405; skill4=float(12.93)@c4_materials.wdl:6406; skill5=float(-407.)@c4_materials.wdl:6407
    // skill6=float(-58.2)@c4_materials.wdl:6408; skill7=float(200.4)@c4_materials.wdl:6409
    {"Mtl_bas_1", {1.2431640625f, 0.8603515625f, 0.9560546875f, 12.9296875f, -407.0f, -58.19921875f, 200.3994140625f}, 0x0007fu, {190.0f, 192.0f, 192.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=120@c4_materials.wdl:2431; ambient_green=175@c4_materials.wdl:2432; ambient_blue=175@c4_materials.wdl:2433; skill1=float(1.436)@c4_materials.wdl:2435
    // skill2=float(1.148)@c4_materials.wdl:2436; skill3=float(1.340)@c4_materials.wdl:2437; skill4=float(8)@c4_materials.wdl:2438; skill5=float(-821.)@c4_materials.wdl:2439
    // skill6=float(-523.)@c4_materials.wdl:2440; skill7=float(-440)@c4_materials.wdl:2441
    {"Mtl_beko", {1.435546875f, 1.1474609375f, 1.33984375f, 8.0f, -821.0f, -523.0f, -440.0f}, 0x0007fu, {120.0f, 175.0f, 175.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:6101; ambient_green=220@c4_materials.wdl:6102; ambient_blue=220@c4_materials.wdl:6103; skill1=float(1)@c4_materials.wdl:6105
    // skill2=float(1)@c4_materials.wdl:6106; skill3=float(0.700)@c4_materials.wdl:6107; skill4=float(10)@c4_materials.wdl:6108; skill5=float(-440)@c4_materials.wdl:6109
    // skill6=float(-440)@c4_materials.wdl:6110; skill7=float(-440)@c4_materials.wdl:6111
    {"Mtl_bez", {1.0f, 1.0f, 0.69921875f, 10.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {230.0f, 220.0f, 220.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=190@c4_materials.wdl:3944; ambient_green=190@c4_materials.wdl:3945; ambient_blue=190@c4_materials.wdl:3946; skill1=float(1.340)@c4_materials.wdl:3948
    // skill2=float(0.670)@c4_materials.wdl:3949; skill3=float(0.191)@c4_materials.wdl:3950; skill4=float(6.466)@c4_materials.wdl:3951; skill5=float(-433.)@c4_materials.wdl:3952
    // skill6=float(-368.)@c4_materials.wdl:3953; skill7=float(-510.)@c4_materials.wdl:3954
    {"Mtl_bez_kenar", {1.33984375f, 0.669921875f, 0.1904296875f, 6.4658203125f, -433.0f, -368.0f, -510.0f}, 0x0007fu, {190.0f, 190.0f, 190.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=167@c4_materials.wdl:4650; ambient_green=166@c4_materials.wdl:4651; ambient_blue=166@c4_materials.wdl:4652; skill1=float(0.861)@c4_materials.wdl:4654
    // skill2=float(0.479)@c4_materials.wdl:4655; skill3=float(0.287)@c4_materials.wdl:4656; skill4=float(6.466)@c4_materials.wdl:4657; skill5=float(-394.)@c4_materials.wdl:4658
    // skill6=float(-97.0)@c4_materials.wdl:4659; skill7=float(-873.)@c4_materials.wdl:4660
    {"Mtl_bin7", {0.8603515625f, 0.478515625f, 0.2861328125f, 6.4658203125f, -394.0f, -97.0f, -873.0f}, 0x0007fu, {167.0f, 166.0f, 166.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=154@c4_materials.wdl:4582; ambient_green=158@c4_materials.wdl:4583; ambient_blue=163@c4_materials.wdl:4584; skill1=float(0.861)@c4_materials.wdl:4586
    // skill2=float(0.574)@c4_materials.wdl:4587; skill3=float(0.287)@c4_materials.wdl:4588; skill4=float(15.08)@c4_materials.wdl:4589; skill5=float(-368.)@c4_materials.wdl:4590
    // skill6=float(45.22)@c4_materials.wdl:4591; skill7=float(-122.)@c4_materials.wdl:4592
    {"Mtl_bin_10", {0.8603515625f, 0.5732421875f, 0.2861328125f, 15.0791015625f, -368.0f, 45.2197265625f, -122.0f}, 0x0007fu, {154.0f, 158.0f, 163.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=183@c4_materials.wdl:4616; ambient_green=189@c4_materials.wdl:4617; ambient_blue=193@c4_materials.wdl:4618; skill1=float(1.053)@c4_materials.wdl:4620
    // skill2=float(0)@c4_materials.wdl:4621; skill3=float(0.670)@c4_materials.wdl:4622; skill4=float(4.311)@c4_materials.wdl:4623; skill5=float(-84.1)@c4_materials.wdl:4624
    // skill6=float(6.422)@c4_materials.wdl:4625; skill7=float(-84.1)@c4_materials.wdl:4626
    {"Mtl_bin_10_lam", {1.052734375f, 0.0f, 0.669921875f, 4.310546875f, -84.099609375f, 6.421875f, -84.099609375f}, 0x0007fu, {183.0f, 189.0f, 193.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:4212; ambient_green=141@c4_materials.wdl:4213; ambient_blue=141@c4_materials.wdl:4214; skill1=float(0.766)@c4_materials.wdl:4216
    // skill2=float(0.957)@c4_materials.wdl:4217; skill3=float(0.670)@c4_materials.wdl:4218; skill4=float(10.77)@c4_materials.wdl:4219; skill5=float(-510.)@c4_materials.wdl:4220
    // skill6=float(-510.)@c4_materials.wdl:4221; skill7=float(-536.)@c4_materials.wdl:4222
    {"Mtl_bin_12", {0.765625f, 0.9560546875f, 0.669921875f, 10.76953125f, -510.0f, -510.0f, -536.0f}, 0x0007fu, {140.0f, 141.0f, 141.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=166@c4_materials.wdl:4178; ambient_green=166@c4_materials.wdl:4179; ambient_blue=167@c4_materials.wdl:4180; skill1=float(0.766)@c4_materials.wdl:4182
    // skill2=float(0.766)@c4_materials.wdl:4183; skill3=float(0.957)@c4_materials.wdl:4184; skill4=float(8)@c4_materials.wdl:4185; skill5=float(32.28)@c4_materials.wdl:4186
    // skill6=float(-446.)@c4_materials.wdl:4187; skill7=float(-459.)@c4_materials.wdl:4188
    {"Mtl_bin_12_b", {0.765625f, 0.765625f, 0.9560546875f, 8.0f, 32.279296875f, -446.0f, -459.0f}, 0x0007fu, {166.0f, 166.0f, 167.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=112@c4_materials.wdl:4144; ambient_green=116@c4_materials.wdl:4145; ambient_blue=112@c4_materials.wdl:4146; skill1=float(1.348)@c4_materials.wdl:4148
    // skill2=float(0.600)@c4_materials.wdl:4149; skill3=float(0.479)@c4_materials.wdl:4150; skill4=float(8)@c4_materials.wdl:4151; skill5=float(-1080)@c4_materials.wdl:4152
    // skill6=float(-536.)@c4_materials.wdl:4153; skill7=float(-575.)@c4_materials.wdl:4154
    {"Mtl_bin_13", {1.34765625f, 0.599609375f, 0.478515625f, 8.0f, -1080.0f, -536.0f, -575.0f}, 0x0007fu, {112.0f, 116.0f, 112.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:4110; ambient_green=145@c4_materials.wdl:4111; ambient_blue=145@c4_materials.wdl:4112; skill1=float(0.670)@c4_materials.wdl:4114
    // skill2=float(0.766)@c4_materials.wdl:4115; skill3=float(0.479)@c4_materials.wdl:4116; skill4=float(6.466)@c4_materials.wdl:4117; skill5=float(-32.3)@c4_materials.wdl:4118
    // skill6=float(-329.)@c4_materials.wdl:4119; skill7=float(-278.)@c4_materials.wdl:4120
    {"Mtl_bin_14", {0.669921875f, 0.765625f, 0.478515625f, 6.4658203125f, -32.2998046875f, -329.0f, -278.0f}, 0x0007fu, {145.0f, 145.0f, 145.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=131@c4_materials.wdl:4684; ambient_green=134@c4_materials.wdl:4685; ambient_blue=135@c4_materials.wdl:4686; skill1=float(0.861)@c4_materials.wdl:4688
    // skill2=float(0.479)@c4_materials.wdl:4689; skill3=float(0.287)@c4_materials.wdl:4690; skill4=float(6.466)@c4_materials.wdl:4691; skill5=float(-394.)@c4_materials.wdl:4692
    // skill6=float(-97.0)@c4_materials.wdl:4693; skill7=float(-873.)@c4_materials.wdl:4694
    {"Mtl_bin_8", {0.8603515625f, 0.478515625f, 0.2861328125f, 6.4658203125f, -394.0f, -97.0f, -873.0f}, 0x0007fu, {131.0f, 134.0f, 135.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=141@c4_materials.wdl:4752; ambient_green=141@c4_materials.wdl:4753; ambient_blue=137@c4_materials.wdl:4754; skill1=float(1.531)@c4_materials.wdl:4756
    // skill2=float(0.861)@c4_materials.wdl:4757; skill3=float(0.574)@c4_materials.wdl:4758; skill4=float(4.311)@c4_materials.wdl:4759; skill5=float(-407.)@c4_materials.wdl:4760
    // skill6=float(-459.)@c4_materials.wdl:4761; skill7=float(-329.)@c4_materials.wdl:4762
    {"Mtl_bin_9", {1.5302734375f, 0.8603515625f, 0.5732421875f, 4.310546875f, -407.0f, -459.0f, -329.0f}, 0x0007fu, {141.0f, 141.0f, 137.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=150@c4_materials.wdl:4718; ambient_green=150@c4_materials.wdl:4719; ambient_blue=160@c4_materials.wdl:4720; skill1=float(0.853)@c4_materials.wdl:4722
    // skill2=float(0.700)@c4_materials.wdl:4723; skill3=float(0.191)@c4_materials.wdl:4724; skill4=float(6.066)@c4_materials.wdl:4725; skill5=float(-873.)@c4_materials.wdl:4726
    // skill6=float(-381.)@c4_materials.wdl:4727; skill7=float(-963.)@c4_materials.wdl:4728
    {"Mtl_bin_alt", {0.8525390625f, 0.69921875f, 0.1904296875f, 6.0654296875f, -873.0f, -381.0f, -963.0f}, 0x0007fu, {150.0f, 150.0f, 160.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=215@c4_materials.wdl:2968; ambient_green=206@c4_materials.wdl:2969; ambient_blue=203@c4_materials.wdl:2970; skill1=float(0.861)@c4_materials.wdl:2972
    // skill2=float(1.431)@c4_materials.wdl:2973; skill3=float(0.099)@c4_materials.wdl:2974; skill4=float(2.155)@c4_materials.wdl:2975; skill5=float(-109.)@c4_materials.wdl:2976
    // skill6=float(-440)@c4_materials.wdl:2977; skill7=float(-213.)@c4_materials.wdl:2978
    {"Mtl_bina_x", {0.8603515625f, 1.4306640625f, 0.0986328125f, 2.154296875f, -109.0f, -440.0f, -213.0f}, 0x0007fu, {215.0f, 206.0f, 203.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=244@c4_materials.wdl:4820; ambient_green=230@c4_materials.wdl:4821; ambient_blue=233@c4_materials.wdl:4822; skill1=float(0.861)@c4_materials.wdl:4824
    // skill2=float(0.670)@c4_materials.wdl:4825; skill3=float(0.479)@c4_materials.wdl:4826; skill4=float(8)@c4_materials.wdl:4827; skill5=float(-84.1)@c4_materials.wdl:4828
    // skill6=float(-122.)@c4_materials.wdl:4829; skill7=float(71.09)@c4_materials.wdl:4830
    {"Mtl_binalar_a", {0.8603515625f, 0.669921875f, 0.478515625f, 8.0f, -84.099609375f, -122.0f, 71.08984375f}, 0x0007fu, {244.0f, 230.0f, 233.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=148@c4_materials.wdl:3232; ambient_green=148@c4_materials.wdl:3233; ambient_blue=148@c4_materials.wdl:3234; skill1=float(0.670)@c4_materials.wdl:3236
    // skill2=float(0.670)@c4_materials.wdl:3237; skill3=float(0)@c4_materials.wdl:3238; skill4=float(8.621)@c4_materials.wdl:3239; skill5=float(-252.)@c4_materials.wdl:3240
    // skill6=float(-239.)@c4_materials.wdl:3241; skill7=float(-407.)@c4_materials.wdl:3242
    {"Mtl_cadir", {0.669921875f, 0.669921875f, 0.0f, 8.6201171875f, -252.0f, -239.0f, -407.0f}, 0x0007fu, {148.0f, 148.0f, 148.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=119@c4_materials.wdl:2179; ambient_green=120@c4_materials.wdl:2180; ambient_blue=135@c4_materials.wdl:2181; skill1=float(1.148)@c4_materials.wdl:2183
    // skill2=float(0.766)@c4_materials.wdl:2184; skill3=float(0.479)@c4_materials.wdl:2185; skill4=float(8)@c4_materials.wdl:2186; skill5=float(-303.)@c4_materials.wdl:2187
    // skill6=float(-213.)@c4_materials.wdl:2188; skill7=float(-523.)@c4_materials.wdl:2189
    {"Mtl_cikis", {1.1474609375f, 0.765625f, 0.478515625f, 8.0f, -303.0f, -213.0f, -523.0f}, 0x0007fu, {119.0f, 120.0f, 135.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=135@c4_materials.wdl:2721; ambient_green=137@c4_materials.wdl:2722; ambient_blue=138@c4_materials.wdl:2723; skill1=float(1.340)@c4_materials.wdl:2725
    // skill2=float(0.566)@c4_materials.wdl:2726; skill3=float(0.348)@c4_materials.wdl:2727; skill4=float(6.466)@c4_materials.wdl:2728; skill5=float(-536.)@c4_materials.wdl:2729
    // skill6=float(-536.)@c4_materials.wdl:2730; skill7=float(-536.)@c4_materials.wdl:2731
    {"Mtl_cit", {1.33984375f, 0.5654296875f, 0.34765625f, 6.4658203125f, -536.0f, -536.0f, -536.0f}, 0x0007fu, {135.0f, 137.0f, 138.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=238@c4_materials.wdl:4076; ambient_green=238@c4_materials.wdl:4077; ambient_blue=239@c4_materials.wdl:4078; skill1=float(1.053)@c4_materials.wdl:4080
    // skill2=float(0.670)@c4_materials.wdl:4081; skill3=float(1.148)@c4_materials.wdl:4082; skill4=float(8)@c4_materials.wdl:4083; skill5=float(-510.)@c4_materials.wdl:4084
    // skill6=float(-510.)@c4_materials.wdl:4085; skill7=float(-510.)@c4_materials.wdl:4086
    {"Mtl_cubuk_2", {1.052734375f, 0.669921875f, 1.1474609375f, 8.0f, -510.0f, -510.0f, -510.0f}, 0x0007fu, {238.0f, 238.0f, 239.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=116@c4_materials.wdl:2687; ambient_green=116@c4_materials.wdl:2688; ambient_blue=114@c4_materials.wdl:2689; skill1=float(0.957)@c4_materials.wdl:2691
    // skill2=float(0.479)@c4_materials.wdl:2692; skill3=float(0.670)@c4_materials.wdl:2693; skill4=float(8)@c4_materials.wdl:2694; skill5=float(-174.)@c4_materials.wdl:2695
    // skill6=float(-291.)@c4_materials.wdl:2696; skill7=float(-433.)@c4_materials.wdl:2697
    {"Mtl_d2", {0.9560546875f, 0.478515625f, 0.669921875f, 8.0f, -174.0f, -291.0f, -433.0f}, 0x0007fu, {116.0f, 116.0f, 114.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=154@c4_materials.wdl:2397; ambient_green=145@c4_materials.wdl:2398; ambient_blue=128@c4_materials.wdl:2399; skill1=float(1.244)@c4_materials.wdl:2401
    // skill2=float(0.670)@c4_materials.wdl:2402; skill3=float(0.191)@c4_materials.wdl:2403; skill4=float(6.466)@c4_materials.wdl:2404; skill5=float(-433.)@c4_materials.wdl:2405
    // skill6=float(-575.)@c4_materials.wdl:2406; skill7=float(-316.)@c4_materials.wdl:2407
    {"Mtl_depo", {1.2431640625f, 0.669921875f, 0.1904296875f, 6.4658203125f, -433.0f, -575.0f, -316.0f}, 0x0007fu, {154.0f, 145.0f, 128.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=100@c4_materials.wdl:2653; ambient_green=100@c4_materials.wdl:2654; ambient_blue=100@c4_materials.wdl:2655; skill1=float(1.080)@c4_materials.wdl:2657
    // skill2=float(0.394)@c4_materials.wdl:2658; skill3=float(0.179)@c4_materials.wdl:2659; skill4=float(8)@c4_materials.wdl:2660; skill5=float(-440)@c4_materials.wdl:2661
    // skill6=float(-440)@c4_materials.wdl:2662; skill7=float(-440)@c4_materials.wdl:2663
    {"Mtl_dev_2", {1.0791015625f, 0.3935546875f, 0.1787109375f, 8.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {100.0f, 100.0f, 100.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=166@c4_materials.wdl:2619; ambient_green=164@c4_materials.wdl:2620; ambient_blue=167@c4_materials.wdl:2621; skill1=float(0.670)@c4_materials.wdl:2623
    // skill2=float(0.383)@c4_materials.wdl:2624; skill3=float(0.383)@c4_materials.wdl:2625; skill4=float(8.621)@c4_materials.wdl:2626; skill5=float(-71.1)@c4_materials.wdl:2627
    // skill6=float(-109.)@c4_materials.wdl:2628; skill7=float(-213.)@c4_materials.wdl:2629
    {"Mtl_dev_3", {0.669921875f, 0.3828125f, 0.3828125f, 8.6201171875f, -71.099609375f, -109.0f, -213.0f}, 0x0007fu, {166.0f, 164.0f, 167.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=166@c4_materials.wdl:2585; ambient_green=164@c4_materials.wdl:2586; ambient_blue=164@c4_materials.wdl:2587; skill1=float(0.670)@c4_materials.wdl:2589
    // skill2=float(0.383)@c4_materials.wdl:2590; skill3=float(0.383)@c4_materials.wdl:2591; skill4=float(8.621)@c4_materials.wdl:2592; skill5=float(-265.)@c4_materials.wdl:2593
    // skill6=float(-368.)@c4_materials.wdl:2594; skill7=float(71.09)@c4_materials.wdl:2595
    {"Mtl_dev_4", {0.669921875f, 0.3828125f, 0.3828125f, 8.6201171875f, -265.0f, -368.0f, 71.08984375f}, 0x0007fu, {166.0f, 164.0f, 164.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=161@c4_materials.wdl:5256; ambient_green=164@c4_materials.wdl:5257; ambient_blue=166@c4_materials.wdl:5258; skill1=float(1.150)@c4_materials.wdl:5260
    // skill2=float(0.299)@c4_materials.wdl:5261; skill3=float(1.200)@c4_materials.wdl:5262; skill4=float(6.466)@c4_materials.wdl:5263; skill5=float(-704.)@c4_materials.wdl:5264
    // skill6=float(-265.)@c4_materials.wdl:5265; skill7=float(-148.)@c4_materials.wdl:5266
    {"Mtl_don_2", {1.1494140625f, 0.298828125f, 1.19921875f, 6.4658203125f, -704.0f, -265.0f, -148.0f}, 0x0007fu, {161.0f, 164.0f, 166.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=169@c4_materials.wdl:5341; ambient_green=169@c4_materials.wdl:5342; ambient_blue=169@c4_materials.wdl:5343; skill1=float(1.340)@c4_materials.wdl:5345
    // skill2=float(0.383)@c4_materials.wdl:5346; skill3=float(1.199)@c4_materials.wdl:5347; skill4=float(8)@c4_materials.wdl:5348; skill5=float(-440)@c4_materials.wdl:5349
    // skill6=float(-440)@c4_materials.wdl:5350; skill7=float(-440)@c4_materials.wdl:5351
    {"Mtl_don_ok", {1.33984375f, 0.3828125f, 1.1982421875f, 8.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {169.0f, 169.0f, 169.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=160@c4_materials.wdl:5805; ambient_green=161@c4_materials.wdl:5806; ambient_blue=163@c4_materials.wdl:5807; skill1=float(0.957)@c4_materials.wdl:5809
    // skill2=float(0.574)@c4_materials.wdl:5810; skill3=float(0.766)@c4_materials.wdl:5811; skill4=float(8)@c4_materials.wdl:5812; skill5=float(-407.)@c4_materials.wdl:5813
    // skill6=float(-355.)@c4_materials.wdl:5814; skill7=float(-278.)@c4_materials.wdl:5815
    {"Mtl_drek", {0.9560546875f, 0.5732421875f, 0.765625f, 8.0f, -407.0f, -355.0f, -278.0f}, 0x0007fu, {160.0f, 161.0f, 163.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=175@c4_materials.wdl:4786; ambient_green=178@c4_materials.wdl:4787; ambient_blue=213@c4_materials.wdl:4788; skill1=float(1.100)@c4_materials.wdl:4790
    // skill2=float(0.666)@c4_materials.wdl:4791; skill3=float(0.799)@c4_materials.wdl:4792; skill4=float(16)@c4_materials.wdl:4793; skill5=float(-549.0)@c4_materials.wdl:4794
    // skill6=float(-440)@c4_materials.wdl:4795; skill7=float(-440)@c4_materials.wdl:4796
    {"Mtl_duvar_3", {1.099609375f, 0.6650390625f, 0.798828125f, 16.0f, -549.0f, -440.0f, -440.0f}, 0x0007fu, {175.0f, 178.0f, 213.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=225@c4_materials.wdl:6050; ambient_green=225@c4_materials.wdl:6051; ambient_blue=225@c4_materials.wdl:6052; skill1=float(1)@c4_materials.wdl:6054
    // skill2=float(0.690)@c4_materials.wdl:6055; skill3=float(0.479)@c4_materials.wdl:6056; skill4=float(8)@c4_materials.wdl:6057; skill5=float(-394.)@c4_materials.wdl:6058
    // skill6=float(-497.)@c4_materials.wdl:6059; skill7=float(-549.)@c4_materials.wdl:6060
    {"Mtl_duvar_a", {1.0f, 0.689453125f, 0.478515625f, 8.0f, -394.0f, -497.0f, -549.0f}, 0x0007fu, {225.0f, 225.0f, 225.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=175@c4_materials.wdl:4042; ambient_green=178@c4_materials.wdl:4043; ambient_blue=213@c4_materials.wdl:4044; skill1=float(0.861)@c4_materials.wdl:4046
    // skill2=float(0.930)@c4_materials.wdl:4047; skill3=float(0.920)@c4_materials.wdl:4048; skill4=float(8)@c4_materials.wdl:4049; skill5=float(-549.)@c4_materials.wdl:4050
    // skill6=float(-440)@c4_materials.wdl:4051; skill7=float(-440)@c4_materials.wdl:4052
    {"Mtl_duvar_ek", {0.8603515625f, 0.9296875f, 0.919921875f, 8.0f, -549.0f, -440.0f, -440.0f}, 0x0007fu, {175.0f, 178.0f, 213.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=148@c4_materials.wdl:3085; ambient_green=148@c4_materials.wdl:3086; ambient_blue=140@c4_materials.wdl:3087; skill1=float(1.148)@c4_materials.wdl:3089
    // skill2=float(0.096)@c4_materials.wdl:3090; skill3=float(0.766)@c4_materials.wdl:3091; skill4=float(10.77)@c4_materials.wdl:3092; skill5=float(-433.)@c4_materials.wdl:3093
    // skill6=float(-420.)@c4_materials.wdl:3094; skill7=float(-808.)@c4_materials.wdl:3095
    {"Mtl_duvar_wan", {1.1474609375f, 0.095703125f, 0.765625f, 10.76953125f, -433.0f, -420.0f, -808.0f}, 0x0007fu, {148.0f, 148.0f, 140.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=129@c4_materials.wdl:4937; ambient_green=203@c4_materials.wdl:4938; ambient_blue=187@c4_materials.wdl:4939; skill1=float(0.370)@c4_materials.wdl:4941
    // skill2=float(0.957)@c4_materials.wdl:4942; skill3=float(0.670)@c4_materials.wdl:4943; skill4=float(4.311)@c4_materials.wdl:4944; skill5=float(-407.)@c4_materials.wdl:4945
    // skill6=float(-834.)@c4_materials.wdl:4946; skill7=float(-679.)@c4_materials.wdl:4947
    {"Mtl_duvset1", {0.369140625f, 0.9560546875f, 0.669921875f, 4.310546875f, -407.0f, -834.0f, -679.0f}, 0x0007fu, {129.0f, 203.0f, 187.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=129@c4_materials.wdl:4971; ambient_green=203@c4_materials.wdl:4972; ambient_blue=187@c4_materials.wdl:4973; skill1=float(0.370)@c4_materials.wdl:4975
    // skill2=float(0.957)@c4_materials.wdl:4976; skill3=float(0.670)@c4_materials.wdl:4977; skill4=float(4.311)@c4_materials.wdl:4978; skill5=float(-407.)@c4_materials.wdl:4979
    // skill6=float(-834.)@c4_materials.wdl:4980; skill7=float(-679.)@c4_materials.wdl:4981
    {"Mtl_duvset2", {0.369140625f, 0.9560546875f, 0.669921875f, 4.310546875f, -407.0f, -834.0f, -679.0f}, 0x0007fu, {129.0f, 203.0f, 187.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=183@c4_materials.wdl:4548; ambient_green=189@c4_materials.wdl:4549; ambient_blue=193@c4_materials.wdl:4550; skill1=float(1.503)@c4_materials.wdl:4552
    // skill2=float(0.670)@c4_materials.wdl:4553; skill3=float(0.000)@c4_materials.wdl:4554; skill4=float(4.311)@c4_materials.wdl:4555; skill5=float(-84.1)@c4_materials.wdl:4556
    // skill6=float(6.422)@c4_materials.wdl:4557; skill7=float(-84.1)@c4_materials.wdl:4558
    {"Mtl_DVR", {1.5029296875f, 0.669921875f, 0.0f, 4.310546875f, -84.099609375f, 6.421875f, -84.099609375f}, 0x0007fu, {183.0f, 189.0f, 193.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:1786; ambient_green=134@c4_materials.wdl:1787; ambient_blue=131@c4_materials.wdl:1788; skill1=float(1.148)@c4_materials.wdl:1790
    // skill2=float(0.957)@c4_materials.wdl:1791; skill3=float(0.479)@c4_materials.wdl:1792; skill4=float(8)@c4_materials.wdl:1793; skill5=float(-440)@c4_materials.wdl:1794
    // skill6=float(-1002)@c4_materials.wdl:1795; skill7=float(109.8)@c4_materials.wdl:1796
    {"Mtl_engel_2", {1.1474609375f, 0.9560546875f, 0.478515625f, 8.0f, -440.0f, -1002.0f, 109.7998046875f}, 0x0007fu, {140.0f, 134.0f, 131.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=244@c4_materials.wdl:4854; ambient_green=230@c4_materials.wdl:4855; ambient_blue=233@c4_materials.wdl:4856; skill1=float(0.861)@c4_materials.wdl:4858
    // skill2=float(0.670)@c4_materials.wdl:4859; skill3=float(0.479)@c4_materials.wdl:4860; skill4=float(8)@c4_materials.wdl:4861; skill5=float(-84.1)@c4_materials.wdl:4862
    // skill6=float(-122.)@c4_materials.wdl:4863; skill7=float(71.09)@c4_materials.wdl:4864
    {"Mtl_ev_2", {0.8603515625f, 0.669921875f, 0.478515625f, 8.0f, -84.099609375f, -122.0f, 71.08984375f}, 0x0007fu, {244.0f, 230.0f, 233.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=203@c4_materials.wdl:2227; ambient_green=196@c4_materials.wdl:2228; ambient_blue=187@c4_materials.wdl:2229; skill1=float(1.436)@c4_materials.wdl:2231
    // skill2=float(0.287)@c4_materials.wdl:2232; skill3=float(0.479)@c4_materials.wdl:2233; skill4=float(17.24)@c4_materials.wdl:2234; skill5=float(-575.0)@c4_materials.wdl:2235
    // skill6=float(-575.0)@c4_materials.wdl:2236; skill7=float(-575.0)@c4_materials.wdl:2237
    {"Mtl_ex_yol_kald", {1.435546875f, 0.2861328125f, 0.478515625f, 17.2392578125f, -575.0f, -575.0f, -575.0f}, 0x0007fu, {203.0f, 196.0f, 187.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:6923; ambient_green=255@c4_materials.wdl:6924; ambient_blue=270@c4_materials.wdl:6925; skill1=float(1.100)@c4_materials.wdl:6927
    // skill2=float(1)@c4_materials.wdl:6928; skill3=float(0.400)@c4_materials.wdl:6929; skill4=float(4.500)@c4_materials.wdl:6930; skill5=float(-440)@c4_materials.wdl:6931
    // skill6=float(-440)@c4_materials.wdl:6932; skill7=float(-440)@c4_materials.wdl:6933
    {"Mtl_exyol_tun_kal", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {255.0f, 255.0f, 270.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=240@c4_materials.wdl:6906; ambient_green=240@c4_materials.wdl:6907; ambient_blue=240@c4_materials.wdl:6908; skill1=float(0.9)@c4_materials.wdl:6910
    // skill2=float(0.9)@c4_materials.wdl:6911; skill3=float(0.400)@c4_materials.wdl:6912; skill4=float(4.500)@c4_materials.wdl:6913; skill5=float(-440)@c4_materials.wdl:6914
    // skill6=float(-440)@c4_materials.wdl:6915; skill7=float(-440)@c4_materials.wdl:6916
    {"Mtl_exyol_tunel", {0.8994140625f, 0.8994140625f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {240.0f, 240.0f, 240.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=103@c4_materials.wdl:3002; ambient_green=116@c4_materials.wdl:3003; ambient_blue=100@c4_materials.wdl:3004; skill1=float(1.531)@c4_materials.wdl:3006
    // skill2=float(0.861)@c4_materials.wdl:3007; skill3=float(0.383)@c4_materials.wdl:3008; skill4=float(4.311)@c4_materials.wdl:3009; skill5=float(-239.)@c4_materials.wdl:3010
    // skill6=float(-84.1)@c4_materials.wdl:3011; skill7=float(96.95)@c4_materials.wdl:3012
    {"Mtl_g2", {1.5302734375f, 0.8603515625f, 0.3828125f, 4.310546875f, -239.0f, -84.099609375f, 96.94921875f}, 0x0007fu, {103.0f, 116.0f, 100.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=134@c4_materials.wdl:3036; ambient_green=137@c4_materials.wdl:3037; ambient_blue=137@c4_materials.wdl:3038; skill1=float(1.436)@c4_materials.wdl:3040
    // skill2=float(0.479)@c4_materials.wdl:3041; skill3=float(0)@c4_materials.wdl:3042; skill4=float(17.24)@c4_materials.wdl:3043; skill5=float(-19.4)@c4_materials.wdl:3044
    // skill6=float(-19.4)@c4_materials.wdl:3045; skill7=float(6.422)@c4_materials.wdl:3046
    {"Mtl_gemi", {1.435546875f, 0.478515625f, 0.0f, 17.2392578125f, -19.3994140625f, -19.3994140625f, 6.421875f}, 0x0007fu, {134.0f, 137.0f, 137.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:6855; ambient_green=255@c4_materials.wdl:6856; ambient_blue=270@c4_materials.wdl:6857; skill1=float(1.100)@c4_materials.wdl:6859
    // skill2=float(1)@c4_materials.wdl:6860; skill3=float(0.400)@c4_materials.wdl:6861; skill4=float(4.500)@c4_materials.wdl:6862; skill5=float(-440)@c4_materials.wdl:6863
    // skill6=float(-440)@c4_materials.wdl:6864; skill7=float(-440)@c4_materials.wdl:6865
    {"Mtl_gg", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {255.0f, 255.0f, 270.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:6872; ambient_green=255@c4_materials.wdl:6873; ambient_blue=270@c4_materials.wdl:6874; skill1=float(1.100)@c4_materials.wdl:6876
    // skill2=float(1)@c4_materials.wdl:6877; skill3=float(0.400)@c4_materials.wdl:6878; skill4=float(4.500)@c4_materials.wdl:6879; skill5=float(-440)@c4_materials.wdl:6880
    // skill6=float(-440)@c4_materials.wdl:6881; skill7=float(-440)@c4_materials.wdl:6882
    {"Mtl_gg2", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {255.0f, 255.0f, 270.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:5967; ambient_green=140@c4_materials.wdl:5968; ambient_blue=140@c4_materials.wdl:5969; skill1=float(1.531)@c4_materials.wdl:5971
    // skill2=float(0.861)@c4_materials.wdl:5972; skill3=float(1.199)@c4_materials.wdl:5973; skill4=float(8)@c4_materials.wdl:5974; skill5=float(-239.)@c4_materials.wdl:5975
    // skill6=float(-200.)@c4_materials.wdl:5976; skill7=float(-135.)@c4_materials.wdl:5977
    {"Mtl_gise_cati", {1.5302734375f, 0.8603515625f, 1.1982421875f, 8.0f, -239.0f, -200.0f, -135.0f}, 0x0007fu, {140.0f, 140.0f, 140.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=103@c4_materials.wdl:3978; ambient_green=105@c4_materials.wdl:3979; ambient_blue=111@c4_materials.wdl:3980; skill1=float(1.148)@c4_materials.wdl:3982
    // skill2=float(0.670)@c4_materials.wdl:3983; skill3=float(0)@c4_materials.wdl:3984; skill4=float(8)@c4_materials.wdl:3985; skill5=float(-303.)@c4_materials.wdl:3986
    // skill6=float(-562.)@c4_materials.wdl:3987; skill7=float(-355.)@c4_materials.wdl:3988
    {"Mtl_gise_koru", {1.1474609375f, 0.669921875f, 0.0f, 8.0f, -303.0f, -562.0f, -355.0f}, 0x0007fu, {103.0f, 105.0f, 111.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:5895; ambient_green=230@c4_materials.wdl:5896; ambient_blue=230@c4_materials.wdl:5897; skill1=float(0.666)@c4_materials.wdl:5899
    // skill2=float(0)@c4_materials.wdl:5900; skill3=float(0.766)@c4_materials.wdl:5901; skill4=float(2.155)@c4_materials.wdl:5902; skill5=float(-407.)@c4_materials.wdl:5903
    // skill6=float(575.5)@c4_materials.wdl:5904; skill7=float(756.5)@c4_materials.wdl:5905
    {"Mtl_gise_reklam", {0.6650390625f, 0.0f, 0.765625f, 2.154296875f, -407.0f, 575.5f, 756.5f}, 0x0007fu, {230.0f, 230.0f, 230.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=190@c4_materials.wdl:6433; ambient_green=192@c4_materials.wdl:6434; ambient_blue=192@c4_materials.wdl:6435; skill1=float(1.244)@c4_materials.wdl:6437
    // skill2=float(0.861)@c4_materials.wdl:6438; skill3=float(0.957)@c4_materials.wdl:6439; skill4=float(12.93)@c4_materials.wdl:6440; skill5=float(-407.)@c4_materials.wdl:6441
    // skill6=float(-58.2)@c4_materials.wdl:6442; skill7=float(200.4)@c4_materials.wdl:6443
    {"Mtl_ins_kork", {1.2431640625f, 0.8603515625f, 0.9560546875f, 12.9296875f, -407.0f, -58.19921875f, 200.3994140625f}, 0x0007fu, {190.0f, 192.0f, 192.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=157@c4_materials.wdl:1752; ambient_green=160@c4_materials.wdl:1753; ambient_blue=155@c4_materials.wdl:1754; skill1=float(0.766)@c4_materials.wdl:1756
    // skill2=float(0.766)@c4_materials.wdl:1757; skill3=float(0)@c4_materials.wdl:1758; skill4=float(17.24)@c4_materials.wdl:1759; skill5=float(-161.)@c4_materials.wdl:1760
    // skill6=float(-640.)@c4_materials.wdl:1761; skill7=float(-601.)@c4_materials.wdl:1762
    {"Mtl_insaat_bez", {0.765625f, 0.765625f, 0.0f, 17.2392578125f, -161.0f, -640.0f, -601.0f}, 0x0007fu, {157.0f, 160.0f, 155.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=222@c4_materials.wdl:4480; ambient_green=222@c4_materials.wdl:4481; ambient_blue=222@c4_materials.wdl:4482; skill1=float(0.861)@c4_materials.wdl:4484
    // skill2=float(0.574)@c4_materials.wdl:4485; skill3=float(0.766)@c4_materials.wdl:4486; skill4=float(8)@c4_materials.wdl:4487; skill5=float(-187.)@c4_materials.wdl:4488
    // skill6=float(-97.0)@c4_materials.wdl:4489; skill7=float(-510.)@c4_materials.wdl:4490
    {"Mtl_k5", {0.8603515625f, 0.5732421875f, 0.765625f, 8.0f, -187.0f, -97.0f, -510.0f}, 0x0007fu, {222.0f, 222.0f, 222.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=117@c4_materials.wdl:3740; ambient_green=125@c4_materials.wdl:3741; ambient_blue=129@c4_materials.wdl:3742; skill1=float(1.100)@c4_materials.wdl:3744
    // skill2=float(1)@c4_materials.wdl:3745; skill3=float(0.670)@c4_materials.wdl:3746; skill4=float(6.466)@c4_materials.wdl:3747; skill5=float(-575.)@c4_materials.wdl:3748
    // skill6=float(-679.)@c4_materials.wdl:3749; skill7=float(-717.)@c4_materials.wdl:3750
    {"Mtl_k_eyaz", {1.099609375f, 1.0f, 0.669921875f, 6.4658203125f, -575.0f, -679.0f, -717.0f}, 0x0007fu, {117.0f, 125.0f, 129.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=164@c4_materials.wdl:2918; ambient_green=164@c4_materials.wdl:2919; ambient_blue=164@c4_materials.wdl:2920; skill1=float(0.766)@c4_materials.wdl:2922
    // skill2=float(0.550)@c4_materials.wdl:2923; skill3=float(0.555)@c4_materials.wdl:2924-2925; skill4=float(4.311)@c4_materials.wdl:2926; skill5=float(-84.1)@c4_materials.wdl:2927
    // skill6=float(-97.0)@c4_materials.wdl:2928; skill7=float(-316.)@c4_materials.wdl:2929
    {"Mtl_kafa", {0.765625f, 0.5498046875f, 0.5546875f, 4.310546875f, -84.099609375f, -97.0f, -316.0f}, 0x0007fu, {164.0f, 164.0f, 164.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=225@c4_materials.wdl:5424; ambient_green=218@c4_materials.wdl:5425; ambient_blue=213@c4_materials.wdl:5426; skill1=float(0.861)@c4_materials.wdl:5428
    // skill2=float(1.340)@c4_materials.wdl:5429; skill3=float(1.199)@c4_materials.wdl:5430; skill4=float(8)@c4_materials.wdl:5431; skill5=float(-523.)@c4_materials.wdl:5432
    // skill6=float(-1080)@c4_materials.wdl:5433; skill7=float(-71.1)@c4_materials.wdl:5434
    {"Mtl_kap2", {0.8603515625f, 1.33984375f, 1.1982421875f, 8.0f, -523.0f, -1080.0f, -71.099609375f}, 0x0007fu, {225.0f, 218.0f, 213.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=172@c4_materials.wdl:5525; ambient_green=178@c4_materials.wdl:5526; ambient_blue=222@c4_materials.wdl:5527; skill1=float(1.340)@c4_materials.wdl:5529
    // skill2=float(1.914)@c4_materials.wdl:5530; skill3=float(1.340)@c4_materials.wdl:5531; skill4=float(15.08)@c4_materials.wdl:5532; skill5=float(122.8)@c4_materials.wdl:5533
    // skill6=float(-84.1)@c4_materials.wdl:5534; skill7=float(161.6)@c4_materials.wdl:5535
    {"Mtl_kapsul1", {1.33984375f, 1.9130859375f, 1.33984375f, 15.0791015625f, 122.7998046875f, -84.099609375f, 161.599609375f}, 0x0007fu, {172.0f, 178.0f, 222.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=215@c4_materials.wdl:5491; ambient_green=61@c4_materials.wdl:5492; ambient_blue=12@c4_materials.wdl:5493; skill1=float(16)@c4_materials.wdl:5495
    // skill2=float(12.82)@c4_materials.wdl:5496; skill3=float(12.34)@c4_materials.wdl:5497; skill4=float(56.03)@c4_materials.wdl:5498; skill5=float(588.4)@c4_materials.wdl:5499
    // skill6=float(717.7)@c4_materials.wdl:5500; skill7=float(523.7)@c4_materials.wdl:5501
    {"Mtl_kapsul_ust2", {16.0f, 12.8193359375f, 12.33984375f, 56.029296875f, 588.3994140625f, 717.69921875f, 523.69921875f}, 0x0007fu, {215.0f, 61.0f, 12.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=109@c4_materials.wdl:2869; ambient_green=123@c4_materials.wdl:2870; ambient_blue=126@c4_materials.wdl:2871; skill1=float(1.148)@c4_materials.wdl:2873
    // skill2=float(0.510)@c4_materials.wdl:2874; skill3=float(0)@c4_materials.wdl:2875; skill4=float(8.621)@c4_materials.wdl:2876; skill5=float(-440)@c4_materials.wdl:2877
    // skill6=float(-440)@c4_materials.wdl:2878; skill7=float(-440)@c4_materials.wdl:2879
    {"Mtl_kon", {1.1474609375f, 0.509765625f, 0.0f, 8.6201171875f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {109.0f, 123.0f, 126.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=129@c4_materials.wdl:3774; ambient_green=126@c4_materials.wdl:3775; ambient_blue=128@c4_materials.wdl:3776; skill1=float(1.000)@c4_materials.wdl:3778
    // skill2=float(0.757)@c4_materials.wdl:3779; skill3=float(1.000)@c4_materials.wdl:3780; skill4=float(10.93)@c4_materials.wdl:3781; skill5=float(-213.)@c4_materials.wdl:3782
    // skill6=float(-342.)@c4_materials.wdl:3783; skill7=float(-485.)@c4_materials.wdl:3784
    {"Mtl_koru", {1.0f, 0.7568359375f, 1.0f, 10.9296875f, -213.0f, -342.0f, -485.0f}, 0x0007fu, {129.0f, 126.0f, 128.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=270@c4_materials.wdl:6515; ambient_green=270@c4_materials.wdl:6516; ambient_blue=270@c4_materials.wdl:6517; skill1=float(0.900)@c4_materials.wdl:6519
    // skill2=float(0.700)@c4_materials.wdl:6520; skill3=float(0.320)@c4_materials.wdl:6521; skill4=float(4)@c4_materials.wdl:6522; skill5=float(-440)@c4_materials.wdl:6523
    // skill6=float(-440)@c4_materials.wdl:6524; skill7=float(-440)@c4_materials.wdl:6525
    {"Mtl_kule_tut", {0.8994140625f, 0.69921875f, 0.3193359375f, 4.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {270.0f, 270.0f, 270.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=149@c4_materials.wdl:5559; ambient_green=143@c4_materials.wdl:5560; ambient_blue=145@c4_materials.wdl:5561; skill1=float(0.957)@c4_materials.wdl:5563
    // skill2=float(0.574)@c4_materials.wdl:5564; skill3=float(0.670)@c4_materials.wdl:5565; skill4=float(8)@c4_materials.wdl:5566; skill5=float(-440.)@c4_materials.wdl:5567
    // skill6=float(-440.)@c4_materials.wdl:5568; skill7=float(-440.)@c4_materials.wdl:5569
    {"Mtl_kuleler", {0.9560546875f, 0.5732421875f, 0.669921875f, 8.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {149.0f, 143.0f, 145.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=150@c4_materials.wdl:5189; ambient_green=150@c4_materials.wdl:5190; ambient_blue=150@c4_materials.wdl:5191; skill1=float(1.444)@c4_materials.wdl:5193
    // skill2=float(1.127)@c4_materials.wdl:5194; skill3=float(1.199)@c4_materials.wdl:5195; skill4=float(8)@c4_materials.wdl:5196; skill5=float(-303.)@c4_materials.wdl:5197
    // skill6=float(-485.)@c4_materials.wdl:5198; skill7=float(6.422)@c4_materials.wdl:5199
    {"Mtl_lastikler", {1.443359375f, 1.126953125f, 1.1982421875f, 8.0f, -303.0f, -485.0f, 6.421875f}, 0x0007fu, {150.0f, 150.0f, 150.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:764; ambient_green=140@c4_materials.wdl:765; ambient_blue=122@c4_materials.wdl:766; skill1=float(1.053)@c4_materials.wdl:768
    // skill2=float(0.287)@c4_materials.wdl:769; skill3=float(0.191)@c4_materials.wdl:770; skill4=float(2.155)@c4_materials.wdl:771; skill5=float(-704.)@c4_materials.wdl:772
    // skill6=float(-433.)@c4_materials.wdl:773; skill7=float(-756.)@c4_materials.wdl:774
    {"Mtl_loop_reklam", {1.052734375f, 0.2861328125f, 0.1904296875f, 2.154296875f, -704.0f, -433.0f, -756.0f}, 0x0007fu, {145.0f, 140.0f, 122.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=146@c4_materials.wdl:3604; ambient_green=146@c4_materials.wdl:3605; ambient_blue=146@c4_materials.wdl:3606; skill1=float(1.148)@c4_materials.wdl:3608
    // skill2=float(0.574)@c4_materials.wdl:3609; skill3=float(3.254)@c4_materials.wdl:3610; skill4=float(8)@c4_materials.wdl:3611; skill5=float(-265.)@c4_materials.wdl:3612
    // skill6=float(-440)@c4_materials.wdl:3613; skill7=float(-440)@c4_materials.wdl:3614
    {"Mtl_luna", {1.1474609375f, 0.5732421875f, 3.25390625f, 8.0f, -265.0f, -440.0f, -440.0f}, 0x0007fu, {146.0f, 146.0f, 146.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=149@c4_materials.wdl:2835; ambient_green=143@c4_materials.wdl:2836; ambient_blue=145@c4_materials.wdl:2837; skill1=float(0.957)@c4_materials.wdl:2839
    // skill2=float(0.574)@c4_materials.wdl:2840; skill3=float(0.670)@c4_materials.wdl:2841; skill4=float(8)@c4_materials.wdl:2842; skill5=float(-440)@c4_materials.wdl:2843
    // skill6=float(-440)@c4_materials.wdl:2844; skill7=float(-440)@c4_materials.wdl:2845
    {"Mtl_milyo", {0.9560546875f, 0.5732421875f, 0.669921875f, 8.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {149.0f, 143.0f, 145.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=183@c4_materials.wdl:3119; ambient_green=186@c4_materials.wdl:3120; ambient_blue=186@c4_materials.wdl:3121; skill1=float(0.957)@c4_materials.wdl:3123
    // skill2=float(0.574)@c4_materials.wdl:3124; skill3=float(0)@c4_materials.wdl:3125; skill4=float(8.621)@c4_materials.wdl:3126; skill5=float(-440)@c4_materials.wdl:3127
    // skill6=float(-440)@c4_materials.wdl:3128; skill7=float(-440)@c4_materials.wdl:3129
    {"Mtl_NFS", {0.9560546875f, 0.5732421875f, 0.0f, 8.6201171875f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {183.0f, 186.0f, 186.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=143@c4_materials.wdl:3153; ambient_green=143@c4_materials.wdl:3154; ambient_blue=143@c4_materials.wdl:3155; skill1=float(0.766)@c4_materials.wdl:3157
    // skill2=float(0.766)@c4_materials.wdl:3158; skill3=float(0.383)@c4_materials.wdl:3159; skill4=float(8)@c4_materials.wdl:3160; skill5=float(-226.)@c4_materials.wdl:3161
    // skill6=float(-329.)@c4_materials.wdl:3162; skill7=float(-575.)@c4_materials.wdl:3163
    {"Mtl_NFS2", {0.765625f, 0.765625f, 0.3828125f, 8.0f, -226.0f, -329.0f, -575.0f}, 0x0007fu, {143.0f, 143.0f, 143.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:6199; ambient_green=207@c4_materials.wdl:6200; ambient_blue=198@c4_materials.wdl:6201; skill1=float(1.053)@c4_materials.wdl:6203
    // skill2=float(1.148)@c4_materials.wdl:6204; skill3=float(0.957)@c4_materials.wdl:6205; skill4=float(8.621)@c4_materials.wdl:6206; skill5=float(-58.2)@c4_materials.wdl:6207
    // skill6=float(-368.)@c4_materials.wdl:6208; skill7=float(-368.)@c4_materials.wdl:6209
    {"Mtl_orta_bariyel", {1.052734375f, 1.1474609375f, 0.9560546875f, 8.6201171875f, -58.19921875f, -368.0f, -368.0f}, 0x0007fu, {207.0f, 207.0f, 198.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:6335; ambient_green=207@c4_materials.wdl:6336; ambient_blue=198@c4_materials.wdl:6337; skill1=float(1.053)@c4_materials.wdl:6339
    // skill2=float(1.148)@c4_materials.wdl:6340; skill3=float(0.957)@c4_materials.wdl:6341; skill4=float(8.621)@c4_materials.wdl:6342; skill5=float(-58.2)@c4_materials.wdl:6343
    // skill6=float(-368.)@c4_materials.wdl:6344; skill7=float(-368.)@c4_materials.wdl:6345
    {"Mtl_orta_bariyel2", {1.052734375f, 1.1474609375f, 0.9560546875f, 8.6201171875f, -58.19921875f, -368.0f, -368.0f}, 0x0007fu, {207.0f, 207.0f, 198.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:4280; ambient_green=187@c4_materials.wdl:4281; ambient_blue=135@c4_materials.wdl:4282; skill1=float(1.531)@c4_materials.wdl:4284
    // skill2=float(1.627)@c4_materials.wdl:4285; skill3=float(2.201)@c4_materials.wdl:4286; skill4=float(2.155)@c4_materials.wdl:4287; skill5=float(588.4)@c4_materials.wdl:4288
    // skill6=float(-200.)@c4_materials.wdl:4289; skill7=float(-122.)@c4_materials.wdl:4290
    {"Mtl_pervane", {1.5302734375f, 1.626953125f, 2.2001953125f, 2.154296875f, 588.3994140625f, -200.0f, -122.0f}, 0x0007fu, {255.0f, 187.0f, 135.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=167@c4_materials.wdl:3638; ambient_green=170@c4_materials.wdl:3639; ambient_blue=170@c4_materials.wdl:3640; skill1=float(0.766)@c4_materials.wdl:3642
    // skill2=float(1.148)@c4_materials.wdl:3643; skill3=float(2.680)@c4_materials.wdl:3644; skill4=float(8)@c4_materials.wdl:3645; skill5=float(122.8)@c4_materials.wdl:3646
    // skill6=float(-187.)@c4_materials.wdl:3647; skill7=float(-278.)@c4_materials.wdl:3648
    {"Mtl_POP", {0.765625f, 1.1474609375f, 2.6796875f, 8.0f, 122.7998046875f, -187.0f, -278.0f}, 0x0007fu, {167.0f, 170.0f, 170.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=240@c4_materials.wdl:5755; ambient_green=240@c4_materials.wdl:5756; ambient_blue=240@c4_materials.wdl:5757; skill1=float(1.053)@c4_materials.wdl:5759
    // skill2=float(1.053)@c4_materials.wdl:5760; skill3=float(0.700)@c4_materials.wdl:5761; skill4=float(2.621)@c4_materials.wdl:5762; skill5=float(-485.)@c4_materials.wdl:5763
    // skill6=float(-187.)@c4_materials.wdl:5764; skill7=float(-278.)@c4_materials.wdl:5765
    {"Mtl_rampa_01", {1.052734375f, 1.052734375f, 0.69921875f, 2.6201171875f, -485.0f, -187.0f, -278.0f}, 0x0007fu, {240.0f, 240.0f, 240.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=39@c4_materials.wdl:2800; ambient_green=36@c4_materials.wdl:2801; ambient_blue=30@c4_materials.wdl:2802; skill1=float(2.393)@c4_materials.wdl:2804
    // skill2=float(0.670)@c4_materials.wdl:2805; skill3=float(0.479)@c4_materials.wdl:2806; skill4=float(2.155)@c4_materials.wdl:2807; skill5=float(-303.)@c4_materials.wdl:2808
    // skill6=float(717.7)@c4_materials.wdl:2809; skill7=float(-510.)@c4_materials.wdl:2810
    {"Mtl_sahil", {2.392578125f, 0.669921875f, 0.478515625f, 2.154296875f, -303.0f, 717.69921875f, -510.0f}, 0x0007fu, {39.0f, 36.0f, 30.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=155@c4_materials.wdl:6643; ambient_green=155@c4_materials.wdl:6644; ambient_blue=155@c4_materials.wdl:6645; skill1=float(0.766)@c4_materials.wdl:6647
    // skill2=float(0.957)@c4_materials.wdl:6648; skill3=float(0.383)@c4_materials.wdl:6649; skill4=float(19.39)@c4_materials.wdl:6650; skill5=float(-440)@c4_materials.wdl:6651
    // skill6=float(-440)@c4_materials.wdl:6652; skill7=float(-440)@c4_materials.wdl:6653
    {"Mtl_semsiye", {0.765625f, 0.9560546875f, 0.3828125f, 19.3896484375f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {155.0f, 155.0f, 155.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:5054; ambient_green=145@c4_materials.wdl:5055; ambient_blue=151@c4_materials.wdl:5056; skill1=float(1.244)@c4_materials.wdl:5058
    // skill2=float(0.670)@c4_materials.wdl:5059; skill3=float(2.680)@c4_materials.wdl:5060; skill4=float(15.08)@c4_materials.wdl:5061; skill5=float(-329.)@c4_materials.wdl:5062
    // skill6=float(-523.)@c4_materials.wdl:5063; skill7=float(-97.0)@c4_materials.wdl:5064
    {"Mtl_set1", {1.2431640625f, 0.669921875f, 2.6796875f, 15.0791015625f, -329.0f, -523.0f, -97.0f}, 0x0007fu, {145.0f, 145.0f, 151.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=138@c4_materials.wdl:619; ambient_green=100@c4_materials.wdl:620; ambient_blue=22@c4_materials.wdl:621; skill1=float(1.148)@c4_materials.wdl:623
    // skill2=float(0.287)@c4_materials.wdl:624; skill3=float(0.574)@c4_materials.wdl:625; skill4=float(2.155)@c4_materials.wdl:626; skill5=float(-924.)@c4_materials.wdl:627
    // skill6=float(-291.)@c4_materials.wdl:628; skill7=float(-459.)@c4_materials.wdl:629
    {"Mtl_set_dur", {1.1474609375f, 0.2861328125f, 0.5732421875f, 2.154296875f, -924.0f, -291.0f, -459.0f}, 0x0007fu, {138.0f, 100.0f, 22.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:3545; ambient_green=227@c4_materials.wdl:3546; ambient_blue=241@c4_materials.wdl:3547; skill1=float(1.627)@c4_materials.wdl:3549
    // skill2=float(3.541)@c4_materials.wdl:3550; skill3=float(16)@c4_materials.wdl:3551; skill4=float(25.86)@c4_materials.wdl:3552; skill5=float(-161)@c4_materials.wdl:3553
    // skill6=float(265)@c4_materials.wdl:3554; skill7=float(-1067)@c4_materials.wdl:3555
    {"Mtl_SH_anayol", {1.626953125f, 3.5400390625f, 16.0f, 25.859375f, -161.0f, 265.0f, -1067.0f}, 0x0007fu, {230.0f, 227.0f, 241.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=158@c4_materials.wdl:3497; ambient_green=155@c4_materials.wdl:3498; ambient_blue=161@c4_materials.wdl:3499; skill1=float(1.148)@c4_materials.wdl:3501
    // skill2=float(0.670)@c4_materials.wdl:3502; skill3=float(0.670)@c4_materials.wdl:3503; skill4=float(4.311)@c4_materials.wdl:3504; skill5=float(-19.4)@c4_materials.wdl:3505
    // skill6=float(-58.2)@c4_materials.wdl:3506; skill7=float(-71.1)@c4_materials.wdl:3507
    {"Mtl_SH_beton", {1.1474609375f, 0.669921875f, 0.669921875f, 4.310546875f, -19.3994140625f, -58.19921875f, -71.099609375f}, 0x0007fu, {158.0f, 155.0f, 161.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:2275; ambient_green=227@c4_materials.wdl:2276; ambient_blue=241@c4_materials.wdl:2277; skill1=float(1.627)@c4_materials.wdl:2279
    // skill2=float(3.541)@c4_materials.wdl:2280; skill3=float(16)@c4_materials.wdl:2281; skill4=float(25.86)@c4_materials.wdl:2282; skill5=float(-161)@c4_materials.wdl:2283
    // skill6=float(265)@c4_materials.wdl:2284; skill7=float(-1067)@c4_materials.wdl:2285
    {"Mtl_SH_denekop_ek", {1.626953125f, 3.5400390625f, 16.0f, 25.859375f, -161.0f, 265.0f, -1067.0f}, 0x0007fu, {230.0f, 227.0f, 241.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:2323; ambient_green=227@c4_materials.wdl:2324; ambient_blue=241@c4_materials.wdl:2325; skill1=float(1.627)@c4_materials.wdl:2327
    // skill2=float(0.800)@c4_materials.wdl:2328; skill3=float(8.423)@c4_materials.wdl:2329; skill4=float(24.27)@c4_materials.wdl:2330; skill5=float(-252)@c4_materials.wdl:2331
    // skill6=float(19.35)@c4_materials.wdl:2332; skill7=float(-446)@c4_materials.wdl:2333
    {"Mtl_SH_ex_yol", {1.626953125f, 0.7998046875f, 8.4228515625f, 24.26953125f, -252.0f, 19.349609375f, -446.0f}, 0x0007fu, {230.0f, 227.0f, 241.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=120@c4_materials.wdl:3319; ambient_green=120@c4_materials.wdl:3320; ambient_blue=120@c4_materials.wdl:3321; skill1=float(1.700)@c4_materials.wdl:3323
    // skill2=float(0.200)@c4_materials.wdl:3324; skill3=float(0.800)@c4_materials.wdl:3325; skill4=float(4.500)@c4_materials.wdl:3326; skill5=float(-440)@c4_materials.wdl:3327
    // skill6=float(-440)@c4_materials.wdl:3328; skill7=float(-440)@c4_materials.wdl:3329
    {"Mtl_SH_gecis", {1.69921875f, 0.19921875f, 0.7998046875f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {120.0f, 120.0f, 120.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=131@c4_materials.wdl:3449; ambient_green=128@c4_materials.wdl:3450; ambient_blue=125@c4_materials.wdl:3451; skill1=float(1.253)@c4_materials.wdl:3453
    // skill2=float(0.850)@c4_materials.wdl:3454; skill3=float(0)@c4_materials.wdl:3455; skill4=float(4.311)@c4_materials.wdl:3456; skill5=float(-821.)@c4_materials.wdl:3457
    // skill6=float(303.8)@c4_materials.wdl:3458; skill7=float(-989.)@c4_materials.wdl:3459
    {"Mtl_SH_kule", {1.2529296875f, 0.849609375f, 0.0f, 4.310546875f, -821.0f, 303.7998046875f, -989.0f}, 0x0007fu, {131.0f, 128.0f, 125.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=131@c4_materials.wdl:3401; ambient_green=128@c4_materials.wdl:3402; ambient_blue=125@c4_materials.wdl:3403; skill1=float(1.253)@c4_materials.wdl:3405
    // skill2=float(0.650)@c4_materials.wdl:3406; skill3=float(0)@c4_materials.wdl:3407; skill4=float(4.311)@c4_materials.wdl:3408; skill5=float(-226.)@c4_materials.wdl:3409
    // skill6=float(-239.)@c4_materials.wdl:3410; skill7=float(-989.)@c4_materials.wdl:3411
    {"Mtl_SH_kule2", {1.2529296875f, 0.6494140625f, 0.0f, 4.310546875f, -226.0f, -239.0f, -989.0f}, 0x0007fu, {131.0f, 128.0f, 125.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=160@c4_materials.wdl:3353; ambient_green=160@c4_materials.wdl:3354; ambient_blue=160@c4_materials.wdl:3355; skill1=float(1.200)@c4_materials.wdl:3357
    // skill2=float(0.700)@c4_materials.wdl:3358; skill3=float(0.400)@c4_materials.wdl:3359; skill4=float(4.500)@c4_materials.wdl:3360; skill5=float(-440)@c4_materials.wdl:3361
    // skill6=float(-440)@c4_materials.wdl:3362; skill7=float(-440)@c4_materials.wdl:3363
    {"Mtl_SH_zgisealt", {1.19921875f, 0.69921875f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {160.0f, 160.0f, 160.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=270@c4_materials.wdl:6474; ambient_green=270@c4_materials.wdl:6475; ambient_blue=270@c4_materials.wdl:6476; skill1=float(1.100)@c4_materials.wdl:6478
    // skill2=float(1.500)@c4_materials.wdl:6479; skill3=float(1.220)@c4_materials.wdl:6480; skill4=float(8)@c4_materials.wdl:6481; skill5=float(-440)@c4_materials.wdl:6482
    // skill6=float(-440)@c4_materials.wdl:6483; skill7=float(-440)@c4_materials.wdl:6484
    {"Mtl_silindir", {1.099609375f, 1.5f, 1.2197265625f, 8.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {270.0f, 270.0f, 270.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=70@c4_materials.wdl:3808; ambient_green=70@c4_materials.wdl:3809; ambient_blue=70@c4_materials.wdl:3810; skill1=float(1.340)@c4_materials.wdl:3812
    // skill2=float(1.340)@c4_materials.wdl:3813; skill3=float(0)@c4_materials.wdl:3814; skill4=float(21.55)@c4_materials.wdl:3815; skill5=float(-97.0)@c4_materials.wdl:3816
    // skill6=float(19.35)@c4_materials.wdl:3817; skill7=float(122.8)@c4_materials.wdl:3818
    {"Mtl_st_layt", {1.33984375f, 1.33984375f, 0.0f, 21.5498046875f, -97.0f, 19.349609375f, 122.7998046875f}, 0x0007fu, {70.0f, 70.0f, 70.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=126@c4_materials.wdl:5290; ambient_green=131@c4_materials.wdl:5291; ambient_blue=148@c4_materials.wdl:5292; skill1=float(1.818)@c4_materials.wdl:5294
    // skill2=float(0.383)@c4_materials.wdl:5295; skill3=float(0.383)@c4_materials.wdl:5296; skill4=float(2.155)@c4_materials.wdl:5297; skill5=float(187.4)@c4_materials.wdl:5298
    // skill6=float(-898.)@c4_materials.wdl:5299; skill7=float(-885.)@c4_materials.wdl:5300
    {"Mtl_stop", {1.8173828125f, 0.3828125f, 0.3828125f, 2.154296875f, 187.3994140625f, -898.0f, -885.0f}, 0x0007fu, {126.0f, 131.0f, 148.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=155@c4_materials.wdl:3706; ambient_green=152@c4_materials.wdl:3707; ambient_blue=152@c4_materials.wdl:3708; skill1=float(0.766)@c4_materials.wdl:3710
    // skill2=float(0.574)@c4_materials.wdl:3711; skill3=float(1.053)@c4_materials.wdl:3712; skill4=float(8)@c4_materials.wdl:3713; skill5=float(-71.1)@c4_materials.wdl:3714
    // skill6=float(19.35)@c4_materials.wdl:3715; skill7=float(45.22)@c4_materials.wdl:3716
    {"Mtl_tabela_8_9", {0.765625f, 0.5732421875f, 1.052734375f, 8.0f, -71.099609375f, 19.349609375f, 45.2197265625f}, 0x0007fu, {155.0f, 152.0f, 152.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=193@c4_materials.wdl:2487; ambient_green=181@c4_materials.wdl:2488; ambient_blue=177@c4_materials.wdl:2489; skill1=float(1.148)@c4_materials.wdl:2491
    // skill2=float(0.861)@c4_materials.wdl:2492; skill3=float(0.191)@c4_materials.wdl:2493; skill4=float(8)@c4_materials.wdl:2494; skill5=float(-440)@c4_materials.wdl:2495
    // skill6=float(-440)@c4_materials.wdl:2496; skill7=float(-440)@c4_materials.wdl:2497
    {"Mtl_temel", {1.1474609375f, 0.8603515625f, 0.1904296875f, 8.0f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {193.0f, 181.0f, 177.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:4514; ambient_green=207@c4_materials.wdl:4515; ambient_blue=207@c4_materials.wdl:4516; skill1=float(1.100)@c4_materials.wdl:4518
    // skill2=float(0.670)@c4_materials.wdl:4519; skill3=float(0.287)@c4_materials.wdl:4520; skill4=float(17.24)@c4_materials.wdl:4521; skill5=float(-440)@c4_materials.wdl:4522
    // skill6=float(-440)@c4_materials.wdl:4523; skill7=float(-440)@c4_materials.wdl:4524
    {"Mtl_topak_y", {1.099609375f, 0.669921875f, 0.2861328125f, 17.2392578125f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {207.0f, 207.0f, 207.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=186@c4_materials.wdl:4378; ambient_green=186@c4_materials.wdl:4379; ambient_blue=189@c4_materials.wdl:4380; skill1=float(0.861)@c4_materials.wdl:4382
    // skill2=float(0.574)@c4_materials.wdl:4383; skill3=float(0.574)@c4_materials.wdl:4384; skill4=float(4.311)@c4_materials.wdl:4385; skill5=float(-342.)@c4_materials.wdl:4386
    // skill6=float(-459.)@c4_materials.wdl:4387; skill7=float(-433.)@c4_materials.wdl:4388
    {"Mtl_topkapi", {0.8603515625f, 0.5732421875f, 0.5732421875f, 4.310546875f, -342.0f, -459.0f, -433.0f}, 0x0007fu, {186.0f, 186.0f, 189.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=204@c4_materials.wdl:6609; ambient_green=195@c4_materials.wdl:6610; ambient_blue=195@c4_materials.wdl:6611; skill1=float(1.053)@c4_materials.wdl:6613
    // skill2=float(0.574)@c4_materials.wdl:6614; skill3=float(1.818)@c4_materials.wdl:6615; skill4=float(21.55)@c4_materials.wdl:6616; skill5=float(-433.)@c4_materials.wdl:6617
    // skill6=float(-440)@c4_materials.wdl:6618; skill7=float(-440)@c4_materials.wdl:6619
    {"Mtl_traf_levha", {1.052734375f, 0.5732421875f, 1.8173828125f, 21.5498046875f, -433.0f, -440.0f, -440.0f}, 0x0007fu, {204.0f, 195.0f, 195.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:6150; ambient_green=255@c4_materials.wdl:6151; ambient_blue=255@c4_materials.wdl:6152; skill1=float(0.957)@c4_materials.wdl:6154
    // skill2=float(0.383)@c4_materials.wdl:6155; skill3=float(0.383)@c4_materials.wdl:6156; skill4=float(10.77)@c4_materials.wdl:6157; skill5=float(-19.4)@c4_materials.wdl:6158
    // skill6=float(6.422)@c4_materials.wdl:6159; skill7=float(19.35)@c4_materials.wdl:6160
    {"Mtl_tunel", {0.9560546875f, 0.3828125f, 0.3828125f, 10.76953125f, -19.3994140625f, 6.421875f, 19.349609375f}, 0x0007fu, {255.0f, 255.0f, 255.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:1962; ambient_green=230@c4_materials.wdl:1963; ambient_blue=230@c4_materials.wdl:1964; skill1=float(1.100)@c4_materials.wdl:1966
    // skill2=float(1)@c4_materials.wdl:1967; skill3=float(0.400)@c4_materials.wdl:1968; skill4=float(4.500)@c4_materials.wdl:1969; skill5=float(-440)@c4_materials.wdl:1970
    // skill6=float(-440)@c4_materials.wdl:1971; skill7=float(-440)@c4_materials.wdl:1972
    {"Mtl_tup_kald", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {230.0f, 230.0f, 230.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:6838; ambient_green=230@c4_materials.wdl:6839; ambient_blue=230@c4_materials.wdl:6840; skill1=float(1.100)@c4_materials.wdl:6842
    // skill2=float(1)@c4_materials.wdl:6843; skill3=float(0.400)@c4_materials.wdl:6844; skill4=float(4.500)@c4_materials.wdl:6845; skill5=float(-440)@c4_materials.wdl:6846
    // skill6=float(-440)@c4_materials.wdl:6847; skill7=float(-440)@c4_materials.wdl:6848
    {"Mtl_tup_kald_dis", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {230.0f, 230.0f, 230.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:6804; ambient_green=230@c4_materials.wdl:6805; ambient_blue=230@c4_materials.wdl:6806; skill1=float(1.100)@c4_materials.wdl:6808
    // skill2=float(1)@c4_materials.wdl:6809; skill3=float(0.400)@c4_materials.wdl:6810; skill4=float(4.500)@c4_materials.wdl:6811; skill5=float(-440)@c4_materials.wdl:6812
    // skill6=float(-440)@c4_materials.wdl:6813; skill7=float(-440)@c4_materials.wdl:6814
    {"Mtl_tup_kald_son", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {230.0f, 230.0f, 230.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=225@c4_materials.wdl:5458; ambient_green=218@c4_materials.wdl:5459; ambient_blue=213@c4_materials.wdl:5460; skill1=float(0.861)@c4_materials.wdl:5462
    // skill2=float(1.340)@c4_materials.wdl:5463; skill3=float(1.199)@c4_materials.wdl:5464; skill4=float(8)@c4_materials.wdl:5465; skill5=float(-523.)@c4_materials.wdl:5466
    // skill6=float(-1080)@c4_materials.wdl:5467; skill7=float(-71.1)@c4_materials.wdl:5468
    {"Mtl_tup_tunel_son", {0.8603515625f, 1.33984375f, 1.1982421875f, 8.0f, -523.0f, -1080.0f, -71.099609375f}, 0x0007fu, {225.0f, 218.0f, 213.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:1996; ambient_green=230@c4_materials.wdl:1997; ambient_blue=230@c4_materials.wdl:1998; skill1=float(1.100)@c4_materials.wdl:2000
    // skill2=float(1)@c4_materials.wdl:2001; skill3=float(0.400)@c4_materials.wdl:2002; skill4=float(4.500)@c4_materials.wdl:2003; skill5=float(-440)@c4_materials.wdl:2004
    // skill6=float(-440)@c4_materials.wdl:2005; skill7=float(-440)@c4_materials.wdl:2006
    {"Mtl_tup_yol", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {230.0f, 230.0f, 230.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:6768; ambient_green=230@c4_materials.wdl:6769; ambient_blue=230@c4_materials.wdl:6770; skill1=float(1.100)@c4_materials.wdl:6772
    // skill2=float(1)@c4_materials.wdl:6773; skill3=float(0.400)@c4_materials.wdl:6774; skill4=float(4.500)@c4_materials.wdl:6775; skill5=float(-440)@c4_materials.wdl:6776
    // skill6=float(-440)@c4_materials.wdl:6777; skill7=float(-440)@c4_materials.wdl:6778
    {"Mtl_tup_yol_dis", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {230.0f, 230.0f, 230.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=230@c4_materials.wdl:6734; ambient_green=230@c4_materials.wdl:6735; ambient_blue=230@c4_materials.wdl:6736; skill1=float(1.100)@c4_materials.wdl:6738
    // skill2=float(1)@c4_materials.wdl:6739; skill3=float(0.400)@c4_materials.wdl:6740; skill4=float(4.500)@c4_materials.wdl:6741; skill5=float(-440)@c4_materials.wdl:6742
    // skill6=float(-440)@c4_materials.wdl:6743; skill7=float(-440)@c4_materials.wdl:6744
    {"Mtl_tup_yol_son", {1.099609375f, 1.0f, 0.3994140625f, 4.5f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {230.0f, 230.0f, 230.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:6301; ambient_green=207@c4_materials.wdl:6302; ambient_blue=198@c4_materials.wdl:6303; skill1=float(1.053)@c4_materials.wdl:6305
    // skill2=float(1.148)@c4_materials.wdl:6306; skill3=float(0.957)@c4_materials.wdl:6307; skill4=float(8.621)@c4_materials.wdl:6308; skill5=float(-58.2)@c4_materials.wdl:6309
    // skill6=float(-368.)@c4_materials.wdl:6310; skill7=float(-368.)@c4_materials.wdl:6311
    {"Mtl_uc_bariyer", {1.052734375f, 1.1474609375f, 0.9560546875f, 8.6201171875f, -58.19921875f, -368.0f, -368.0f}, 0x0007fu, {207.0f, 207.0f, 198.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=175@c4_materials.wdl:3672; ambient_green=169@c4_materials.wdl:3673; ambient_blue=160@c4_materials.wdl:3674; skill1=float(1.100)@c4_materials.wdl:3676
    // skill2=float(0.670)@c4_materials.wdl:3677; skill3=float(0.670)@c4_materials.wdl:3678; skill4=float(2.155)@c4_materials.wdl:3679; skill5=float(-459.)@c4_materials.wdl:3680
    // skill6=float(-446.)@c4_materials.wdl:3681; skill7=float(-446.)@c4_materials.wdl:3682
    {"Mtl_uc_ok", {1.099609375f, 0.669921875f, 0.669921875f, 2.154296875f, -459.0f, -446.0f, -446.0f}, 0x0007fu, {175.0f, 169.0f, 160.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=210@c4_materials.wdl:3910; ambient_green=210@c4_materials.wdl:3911; ambient_blue=210@c4_materials.wdl:3912; skill1=float(1.100)@c4_materials.wdl:3914
    // skill2=float(0.230)@c4_materials.wdl:3915; skill3=float(0.574)@c4_materials.wdl:3916; skill4=float(8)@c4_materials.wdl:3917; skill5=float(-381.)@c4_materials.wdl:3918
    // skill6=float(-394.)@c4_materials.wdl:3919; skill7=float(-381.)@c4_materials.wdl:3920
    {"Mtl_ucgen", {1.099609375f, 0.2294921875f, 0.5732421875f, 8.0f, -381.0f, -394.0f, -381.0f}, 0x0007fu, {210.0f, 210.0f, 210.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=166@c4_materials.wdl:3876; ambient_green=166@c4_materials.wdl:3877; ambient_blue=167@c4_materials.wdl:3878; skill1=float(1.053)@c4_materials.wdl:3880
    // skill2=float(1.148)@c4_materials.wdl:3881; skill3=float(0.670)@c4_materials.wdl:3882; skill4=float(8.621)@c4_materials.wdl:3883; skill5=float(-497.)@c4_materials.wdl:3884
    // skill6=float(-329.)@c4_materials.wdl:3885; skill7=float(-213.)@c4_materials.wdl:3886
    {"Mtl_ucgen_2", {1.052734375f, 1.1474609375f, 0.669921875f, 8.6201171875f, -497.0f, -329.0f, -213.0f}, 0x0007fu, {166.0f, 166.0f, 167.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=219@c4_materials.wdl:2131; ambient_green=210@c4_materials.wdl:2132; ambient_blue=207@c4_materials.wdl:2133; skill1=float(1.244)@c4_materials.wdl:2135
    // skill2=float(0.766)@c4_materials.wdl:2136; skill3=float(1.723)@c4_materials.wdl:2137; skill4=float(6.466)@c4_materials.wdl:2138; skill5=float(96.95)@c4_materials.wdl:2139
    // skill6=float(200.4)@c4_materials.wdl:2140; skill7=float(-1080)@c4_materials.wdl:2141
    {"Mtl_vadi", {1.2431640625f, 0.765625f, 1.72265625f, 6.4658203125f, 96.94921875f, 200.3994140625f, -1080.0f}, 0x0007fu, {219.0f, 210.0f, 207.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=245@c4_materials.wdl:2083; ambient_green=238@c4_materials.wdl:2084; ambient_blue=222@c4_materials.wdl:2085; skill1=float(1.244)@c4_materials.wdl:2087
    // skill2=float(0.383)@c4_materials.wdl:2088; skill3=float(2.105)@c4_materials.wdl:2089; skill4=float(2.155)@c4_materials.wdl:2090; skill5=float(-717.)@c4_materials.wdl:2091
    // skill6=float(-32.3)@c4_materials.wdl:2092; skill7=float(-1080)@c4_materials.wdl:2093
    {"Mtl_vadi_yan", {1.2431640625f, 0.3828125f, 2.1044921875f, 2.154296875f, -717.0f, -32.2998046875f, -1080.0f}, 0x0007fu, {245.0f, 238.0f, 222.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=225@c4_materials.wdl:6084; ambient_green=225@c4_materials.wdl:6085; ambient_blue=225@c4_materials.wdl:6086; skill1=float(1)@c4_materials.wdl:6088
    // skill2=float(0.690)@c4_materials.wdl:6089; skill3=float(0.479)@c4_materials.wdl:6090; skill4=float(8)@c4_materials.wdl:6091; skill5=float(-394.)@c4_materials.wdl:6092
    // skill6=float(-497.)@c4_materials.wdl:6093; skill7=float(-549.)@c4_materials.wdl:6094
    {"Mtl_w_duvar", {1.0f, 0.689453125f, 0.478515625f, 8.0f, -394.0f, -497.0f, -549.0f}, 0x0007fu, {225.0f, 225.0f, 225.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:6267; ambient_green=207@c4_materials.wdl:6268; ambient_blue=198@c4_materials.wdl:6269; skill1=float(1.053)@c4_materials.wdl:6271
    // skill2=float(1.148)@c4_materials.wdl:6272; skill3=float(0.957)@c4_materials.wdl:6273; skill4=float(8.621)@c4_materials.wdl:6274; skill5=float(-58.2)@c4_materials.wdl:6275
    // skill6=float(-368.)@c4_materials.wdl:6276; skill7=float(-368.)@c4_materials.wdl:6277
    {"Mtl_willa_kapi", {1.052734375f, 1.1474609375f, 0.9560546875f, 8.6201171875f, -58.19921875f, -368.0f, -368.0f}, 0x0007fu, {207.0f, 207.0f, 198.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:2035; ambient_green=207@c4_materials.wdl:2036; ambient_blue=207@c4_materials.wdl:2037; skill1=float(1.100)@c4_materials.wdl:2039
    // skill2=float(0.670)@c4_materials.wdl:2040; skill3=float(0.287)@c4_materials.wdl:2041; skill4=float(17.24)@c4_materials.wdl:2042; skill5=float(-440)@c4_materials.wdl:2043
    // skill6=float(-440)@c4_materials.wdl:2044; skill7=float(-440)@c4_materials.wdl:2045
    {"Mtl_y_kum_yan", {1.099609375f, 0.669921875f, 0.2861328125f, 17.2392578125f, -440.0f, -440.0f, -440.0f}, 0x0007fu, {207.0f, 207.0f, 207.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=137@c4_materials.wdl:5854; ambient_green=138@c4_materials.wdl:5855; ambient_blue=138@c4_materials.wdl:5856; skill1=float(0.861)@c4_materials.wdl:5858
    // skill2=float(0.966)@c4_materials.wdl:5859; skill3=float(0.287)@c4_materials.wdl:5860; skill4=float(38.79)@c4_materials.wdl:5861; skill5=float(-58.2)@c4_materials.wdl:5862
    // skill6=float(-97.0)@c4_materials.wdl:5863; skill7=float(-97.0)@c4_materials.wdl:5864
    {"Mtl_yan_oklar", {0.8603515625f, 0.9658203125f, 0.2861328125f, 38.7890625f, -58.19921875f, -97.0f, -97.0f}, 0x0007fu, {137.0f, 138.0f, 138.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=184@c4_materials.wdl:5307; ambient_green=190@c4_materials.wdl:5308; ambient_blue=196@c4_materials.wdl:5309; skill1=float(1.244)@c4_materials.wdl:5311
    // skill2=float(0.383)@c4_materials.wdl:5312; skill3=float(1.627)@c4_materials.wdl:5313; skill4=float(8)@c4_materials.wdl:5314; skill5=float(-627.)@c4_materials.wdl:5315
    // skill6=float(-653.)@c4_materials.wdl:5316; skill7=float(-355.)@c4_materials.wdl:5317
    {"Mtl_yantel", {1.2431640625f, 0.3828125f, 1.626953125f, 8.0f, -627.0f, -653.0f, -355.0f}, 0x0007fu, {184.0f, 190.0f, 196.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=192@c4_materials.wdl:1269; ambient_green=105@c4_materials.wdl:1270; ambient_blue=82@c4_materials.wdl:1271; skill1=float(0.861)@c4_materials.wdl:1273
    // skill2=float(0.479)@c4_materials.wdl:1274; skill3=float(0)@c4_materials.wdl:1275; skill4=float(4.311)@c4_materials.wdl:1276; skill5=float(1079.)@c4_materials.wdl:1277
    // skill6=float(-213.)@c4_materials.wdl:1278; skill7=float(-122.)@c4_materials.wdl:1279
    {"Mtl_yeni_atmos_sag", {0.8603515625f, 0.478515625f, 0.0f, 4.310546875f, 1079.0f, -213.0f, -122.0f}, 0x0007fu, {192.0f, 105.0f, 82.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=178@c4_materials.wdl:1315; ambient_green=103@c4_materials.wdl:1316; ambient_blue=83@c4_materials.wdl:1317; skill1=float(0.861)@c4_materials.wdl:1319
    // skill2=float(0.670)@c4_materials.wdl:1320; skill3=float(0.096)@c4_materials.wdl:1321; skill4=float(2.155)@c4_materials.wdl:1322; skill5=float(109.8)@c4_materials.wdl:1323
    // skill6=float(-1080)@c4_materials.wdl:1324; skill7=float(-497.)@c4_materials.wdl:1325
    {"Mtl_yeni_atmos_sol", {0.8603515625f, 0.669921875f, 0.095703125f, 2.154296875f, 109.7998046875f, -1080.0f, -497.0f}, 0x0007fu, {178.0f, 103.0f, 83.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=236@c4_materials.wdl:1643; ambient_green=204@c4_materials.wdl:1644; ambient_blue=196@c4_materials.wdl:1645; skill1=float(1.244)@c4_materials.wdl:1647
    // skill2=float(1.340)@c4_materials.wdl:1648; skill3=float(0.957)@c4_materials.wdl:1649; skill4=float(12.93)@c4_materials.wdl:1650; skill5=float(-278.)@c4_materials.wdl:1651
    // skill6=float(-433.)@c4_materials.wdl:1652; skill7=float(-730.)@c4_materials.wdl:1653
    {"Mtl_yeni_pal", {1.2431640625f, 1.33984375f, 0.9560546875f, 12.9296875f, -278.0f, -433.0f, -730.0f}, 0x0007fu, {236.0f, 204.0f, 196.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=154@c4_materials.wdl:1609; ambient_green=158@c4_materials.wdl:1610; ambient_blue=146@c4_materials.wdl:1611; skill1=float(0.670)@c4_materials.wdl:1613
    // skill2=float(0.670)@c4_materials.wdl:1614; skill3=float(0)@c4_materials.wdl:1615; skill4=float(21.55)@c4_materials.wdl:1616; skill5=float(-135.)@c4_materials.wdl:1617
    // skill6=float(-355.)@c4_materials.wdl:1618; skill7=float(-316.)@c4_materials.wdl:1619
    {"Mtl_yeni_siteler", {0.669921875f, 0.669921875f, 0.0f, 21.5498046875f, -135.0f, -355.0f, -316.0f}, 0x0007fu, {154.0f, 158.0f, 146.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=219@c4_materials.wdl:1919; ambient_green=210@c4_materials.wdl:1920; ambient_blue=207@c4_materials.wdl:1921; skill1=float(1.244)@c4_materials.wdl:1923
    // skill2=float(0.766)@c4_materials.wdl:1924; skill3=float(1.723)@c4_materials.wdl:1925; skill4=float(6.466)@c4_materials.wdl:1926; skill5=float(96.95)@c4_materials.wdl:1927
    // skill6=float(200.4)@c4_materials.wdl:1928; skill7=float(-1080)@c4_materials.wdl:1929
    {"Mtl_yol_kum", {1.2431640625f, 0.765625f, 1.72265625f, 6.4658203125f, 96.94921875f, 200.3994140625f, -1080.0f}, 0x0007fu, {219.0f, 210.0f, 207.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=169@c4_materials.wdl:5005; ambient_green=170@c4_materials.wdl:5006; ambient_blue=170@c4_materials.wdl:5007; skill1=float(0.957)@c4_materials.wdl:5009
    // skill2=float(0.309)@c4_materials.wdl:5010; skill3=float(0.214)@c4_materials.wdl:5011; skill4=float(4.155)@c4_materials.wdl:5012; skill5=float(-291.)@c4_materials.wdl:5013
    // skill6=float(-303.)@c4_materials.wdl:5014; skill7=float(-303.)@c4_materials.wdl:5015
    {"Mtl_yol_ok", {0.9560546875f, 0.30859375f, 0.2138671875f, 4.154296875f, -291.0f, -303.0f, -303.0f}, 0x0007fu, {169.0f, 170.0f, 170.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=207@c4_materials.wdl:6233; ambient_green=207@c4_materials.wdl:6234; ambient_blue=198@c4_materials.wdl:6235; skill1=float(1.053)@c4_materials.wdl:6237
    // skill2=float(1.148)@c4_materials.wdl:6238; skill3=float(0.957)@c4_materials.wdl:6239; skill4=float(8.621)@c4_materials.wdl:6240; skill5=float(-58.2)@c4_materials.wdl:6241
    // skill6=float(-368.)@c4_materials.wdl:6242; skill7=float(-368.)@c4_materials.wdl:6243
    {"Mtl_z_bariyerler", {1.052734375f, 1.1474609375f, 0.9560546875f, 8.6201171875f, -58.19921875f, -368.0f, -368.0f}, 0x0007fu, {207.0f, 207.0f, 198.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=140@c4_materials.wdl:6001; ambient_green=140@c4_materials.wdl:6002; ambient_blue=140@c4_materials.wdl:6003; skill1=float(1.531)@c4_materials.wdl:6005
    // skill2=float(0.861)@c4_materials.wdl:6006; skill3=float(1.199)@c4_materials.wdl:6007; skill4=float(8)@c4_materials.wdl:6008; skill5=float(-239.)@c4_materials.wdl:6009
    // skill6=float(-200.)@c4_materials.wdl:6010; skill7=float(-135.)@c4_materials.wdl:6011
    {"Mtl_z_kulube2", {1.5302734375f, 0.8603515625f, 1.1982421875f, 8.0f, -239.0f, -200.0f, -135.0f}, 0x0007fu, {140.0f, 140.0f, 140.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 20; payload shared where selected fields are identical.
    // ambient_red=178@c4_materials.wdl:3842; ambient_green=181@c4_materials.wdl:3843; ambient_blue=190@c4_materials.wdl:3844; skill1=float(0.766)@c4_materials.wdl:3846
    // skill2=float(0.957)@c4_materials.wdl:3847; skill3=float(0.670)@c4_materials.wdl:3848; skill4=float(12.93)@c4_materials.wdl:3849; skill5=float(-265.)@c4_materials.wdl:3850
    // skill6=float(-226.)@c4_materials.wdl:3851; skill7=float(-265.)@c4_materials.wdl:3852
    {"Mtl_zeplin", {0.765625f, 0.9560546875f, 0.669921875f, 12.9296875f, -265.0f, -226.0f, -265.0f}, 0x0007fu, {178.0f, 181.0f, 190.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 22; payload shared where selected fields are identical.
    // ambient_red=255@c4_materials.wdl:870; ambient_green=146@c4_materials.wdl:871; ambient_blue=172@c4_materials.wdl:872; skill1=float(2.297)@c4_materials.wdl:874
    // skill2=float(0)@c4_materials.wdl:875; skill3=float(0.574)@c4_materials.wdl:876; skill4=float(2.155)@c4_materials.wdl:877; skill5=float(-666.)@c4_materials.wdl:878
    // skill6=float(-407.)@c4_materials.wdl:879; skill7=float(174.5)@c4_materials.wdl:880; skill9=float(0.029)@c4_materials.wdl:885
    {"Mtl_afis_site", {2.296875f, 0.0f, 0.5732421875f, 2.154296875f, -666.0f, -407.0f, 174.5f, 0.0f, 0.0283203125f}, 0x0017fu, {255.0f, 146.0f, 172.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 22; payload shared where selected fields are identical.
    // ambient_red=145@c4_materials.wdl:764; ambient_green=140@c4_materials.wdl:765; ambient_blue=122@c4_materials.wdl:766; skill1=float(1.053)@c4_materials.wdl:768
    // skill2=float(0.287)@c4_materials.wdl:769; skill3=float(0.191)@c4_materials.wdl:770; skill4=float(2.155)@c4_materials.wdl:771; skill5=float(-704.)@c4_materials.wdl:772
    // skill6=float(-433.)@c4_materials.wdl:773; skill7=float(-756.)@c4_materials.wdl:774; skill9=float(0.102)@c4_materials.wdl:779
    {"Mtl_loop_reklam", {1.052734375f, 0.2861328125f, 0.1904296875f, 2.154296875f, -704.0f, -433.0f, -756.0f, 0.0f, 0.1015625f}, 0x0017fu, {145.0f, 140.0f, 122.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 22; payload shared where selected fields are identical.
    // ambient_red=192@c4_materials.wdl:1269; ambient_green=105@c4_materials.wdl:1270; ambient_blue=82@c4_materials.wdl:1271; skill1=float(0.861)@c4_materials.wdl:1273
    // skill2=float(0.479)@c4_materials.wdl:1274; skill3=float(0)@c4_materials.wdl:1275; skill4=float(4.311)@c4_materials.wdl:1276; skill5=float(1079.)@c4_materials.wdl:1277
    // skill6=float(-213.)@c4_materials.wdl:1278; skill7=float(-122.)@c4_materials.wdl:1279; skill9=float(0.083)@c4_materials.wdl:1284
    {"Mtl_yeni_atmos_sag", {0.8603515625f, 0.478515625f, 0.0f, 4.310546875f, 1079.0f, -213.0f, -122.0f, 0.0f, 0.08203125f}, 0x0017fu, {192.0f, 105.0f, 82.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
    // Representative source profile ID 22; payload shared where selected fields are identical.
    // ambient_red=178@c4_materials.wdl:1315; ambient_green=103@c4_materials.wdl:1316; ambient_blue=83@c4_materials.wdl:1317; skill1=float(0.861)@c4_materials.wdl:1319
    // skill2=float(0.670)@c4_materials.wdl:1320; skill3=float(0.096)@c4_materials.wdl:1321; skill4=float(2.155)@c4_materials.wdl:1322; skill5=float(109.8)@c4_materials.wdl:1323
    // skill6=float(-1080)@c4_materials.wdl:1324; skill7=float(-497.)@c4_materials.wdl:1325; skill9=float(0.083)@c4_materials.wdl:1330
    {"Mtl_yeni_atmos_sol", {0.8603515625f, 0.669921875f, 0.095703125f, 2.154296875f, 109.7998046875f, -1080.0f, -497.0f, 0.0f, 0.08203125f}, 0x0017fu, {178.0f, 103.0f, 83.0f}, {}, {}, {}, 0x007u, 0.0f, 0.0f, false, false, 0u, false, 0.0f, false},
};

constexpr std::array<std::uint16_t, 212> material_constant_variant_bucket_0 = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u,
    16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u,
    32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u, 46u, 47u,
    48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u, 60u, 61u, 62u, 63u,
    64u, 65u, 66u, 67u, 68u, 69u, 70u, 71u, 72u, 73u, 74u, 75u, 76u, 77u, 78u, 79u,
    80u, 81u, 82u, 83u, 84u, 85u, 86u, 87u, 88u, 89u, 90u, 91u, 92u, 93u, 94u, 95u,
    96u, 97u, 98u, 99u, 100u, 101u, 102u, 103u, 104u, 105u, 106u, 107u, 108u, 109u, 110u, 111u,
    112u, 113u, 114u, 115u, 116u, 117u, 118u, 119u, 120u, 121u, 122u, 123u, 124u, 125u, 126u, 127u,
    128u, 129u, 130u, 131u, 132u, 133u, 134u, 135u, 136u, 137u, 138u, 139u, 140u, 141u, 142u, 143u,
    144u, 145u, 146u, 147u, 148u, 149u, 150u, 151u, 152u, 153u, 154u, 155u, 156u, 157u, 158u, 159u,
    160u, 161u, 162u, 163u, 164u, 165u, 166u, 167u, 168u, 169u, 170u, 171u, 172u, 173u, 174u, 175u,
    176u, 177u, 178u, 179u, 180u, 181u, 182u, 183u, 184u, 185u, 186u, 187u, 188u, 189u, 190u, 191u,
    192u, 193u, 194u, 195u, 196u, 197u, 198u, 199u, 200u, 201u, 202u, 203u, 204u, 205u, 206u, 207u,
    208u, 209u, 210u, 211u,
};

constexpr std::array<std::uint16_t, 212> material_constant_variant_bucket_1 = {
    0u, 1u, 2u, 3u, 212u, 5u, 6u, 7u, 8u, 9u, 213u, 214u, 12u, 13u, 14u, 215u,
    16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 216u, 27u, 28u, 29u, 30u, 31u,
    32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 217u, 218u, 44u, 219u, 46u, 47u,
    48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u, 60u, 61u, 62u, 63u,
    64u, 65u, 66u, 67u, 68u, 69u, 70u, 71u, 72u, 73u, 74u, 75u, 76u, 77u, 78u, 79u,
    80u, 81u, 82u, 220u, 84u, 85u, 221u, 87u, 88u, 89u, 90u, 91u, 222u, 93u, 223u, 95u,
    224u, 97u, 98u, 99u, 100u, 101u, 102u, 103u, 104u, 105u, 106u, 107u, 108u, 109u, 110u, 111u,
    112u, 225u, 114u, 226u, 116u, 117u, 227u, 119u, 120u, 121u, 122u, 123u, 124u, 125u, 126u, 127u,
    128u, 129u, 130u, 131u, 132u, 133u, 134u, 135u, 136u, 137u, 228u, 139u, 229u, 141u, 142u, 143u,
    144u, 145u, 146u, 147u, 148u, 149u, 150u, 151u, 152u, 153u, 154u, 155u, 156u, 230u, 158u, 231u,
    160u, 161u, 162u, 163u, 164u, 165u, 232u, 167u, 168u, 169u, 170u, 171u, 172u, 173u, 174u, 175u,
    176u, 177u, 178u, 179u, 180u, 181u, 182u, 183u, 184u, 185u, 186u, 187u, 233u, 189u, 190u, 191u,
    192u, 193u, 194u, 234u, 235u, 197u, 198u, 199u, 200u, 201u, 202u, 203u, 204u, 205u, 206u, 207u,
    236u, 237u, 238u, 239u,
};

constexpr std::array<std::uint16_t, 212> material_constant_variant_bucket_2 = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 240u, 10u, 11u, 241u, 242u, 14u, 15u,
    16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u,
    32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u, 44u, 243u, 46u, 47u,
    48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u, 60u, 61u, 62u, 63u,
    64u, 65u, 66u, 67u, 68u, 69u, 70u, 71u, 72u, 73u, 74u, 75u, 76u, 77u, 78u, 79u,
    80u, 81u, 82u, 83u, 84u, 85u, 86u, 87u, 88u, 89u, 90u, 91u, 92u, 93u, 244u, 245u,
    246u, 97u, 98u, 99u, 100u, 101u, 102u, 103u, 104u, 105u, 106u, 107u, 108u, 109u, 110u, 111u,
    112u, 113u, 114u, 115u, 116u, 117u, 118u, 119u, 120u, 121u, 122u, 123u, 124u, 125u, 126u, 127u,
    128u, 129u, 130u, 131u, 132u, 133u, 134u, 135u, 136u, 137u, 247u, 248u, 249u, 141u, 142u, 143u,
    144u, 145u, 146u, 147u, 148u, 149u, 150u, 151u, 152u, 153u, 154u, 155u, 156u, 157u, 158u, 159u,
    160u, 161u, 162u, 163u, 164u, 165u, 166u, 167u, 168u, 169u, 170u, 171u, 172u, 173u, 174u, 175u,
    176u, 177u, 178u, 179u, 180u, 181u, 182u, 183u, 184u, 185u, 186u, 187u, 188u, 189u, 190u, 191u,
    192u, 193u, 194u, 195u, 196u, 197u, 198u, 199u, 200u, 201u, 202u, 203u, 204u, 205u, 206u, 207u,
    250u, 251u, 252u, 253u,
};

constexpr std::array<std::uint16_t, 212> material_constant_variant_bucket_3 = {
    0u, 1u, 2u, 3u, 212u, 5u, 6u, 7u, 8u, 240u, 213u, 214u, 241u, 242u, 14u, 215u,
    16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 216u, 27u, 28u, 29u, 30u, 31u,
    32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 217u, 218u, 44u, 254u, 46u, 47u,
    48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u, 60u, 61u, 62u, 63u,
    64u, 65u, 66u, 67u, 68u, 69u, 70u, 71u, 72u, 73u, 74u, 75u, 76u, 77u, 78u, 79u,
    80u, 81u, 82u, 220u, 84u, 85u, 221u, 87u, 88u, 89u, 90u, 91u, 222u, 93u, 244u, 245u,
    246u, 97u, 98u, 99u, 100u, 101u, 102u, 103u, 104u, 105u, 106u, 107u, 108u, 109u, 110u, 111u,
    112u, 225u, 114u, 226u, 116u, 117u, 227u, 119u, 120u, 121u, 122u, 123u, 124u, 125u, 126u, 127u,
    128u, 129u, 130u, 131u, 132u, 133u, 134u, 135u, 136u, 137u, 255u, 248u, 256u, 141u, 142u, 143u,
    144u, 145u, 146u, 147u, 148u, 149u, 150u, 151u, 152u, 153u, 154u, 155u, 156u, 230u, 158u, 231u,
    160u, 161u, 162u, 163u, 164u, 165u, 232u, 167u, 168u, 169u, 170u, 171u, 172u, 173u, 174u, 175u,
    176u, 177u, 178u, 179u, 180u, 181u, 182u, 183u, 184u, 185u, 186u, 187u, 233u, 189u, 190u, 191u,
    192u, 193u, 194u, 234u, 235u, 197u, 198u, 199u, 200u, 201u, 202u, 203u, 204u, 205u, 206u, 207u,
    257u, 258u, 259u, 260u,
};

constexpr std::array<std::uint16_t, 212> material_constant_variant_bucket_4 = {
    0u, 1u, 261u, 262u, 263u, 264u, 6u, 7u, 265u, 240u, 10u, 11u, 241u, 242u, 14u, 15u,
    16u, 17u, 266u, 267u, 268u, 269u, 22u, 270u, 271u, 25u, 26u, 272u, 273u, 274u, 275u, 276u,
    277u, 278u, 279u, 280u, 281u, 282u, 283u, 284u, 285u, 286u, 42u, 43u, 287u, 243u, 46u, 288u,
    289u, 290u, 291u, 51u, 292u, 293u, 294u, 295u, 56u, 296u, 58u, 297u, 60u, 298u, 62u, 63u,
    299u, 300u, 301u, 302u, 303u, 304u, 305u, 306u, 72u, 307u, 74u, 308u, 309u, 310u, 78u, 311u,
    312u, 313u, 314u, 83u, 315u, 316u, 317u, 87u, 88u, 89u, 90u, 91u, 92u, 93u, 244u, 245u,
    246u, 318u, 319u, 99u, 100u, 101u, 320u, 321u, 322u, 105u, 323u, 324u, 325u, 109u, 326u, 111u,
    327u, 328u, 329u, 115u, 116u, 330u, 331u, 119u, 332u, 121u, 122u, 123u, 124u, 125u, 333u, 334u,
    335u, 336u, 337u, 131u, 132u, 133u, 134u, 338u, 339u, 340u, 247u, 248u, 249u, 141u, 341u, 143u,
    342u, 145u, 343u, 344u, 345u, 346u, 347u, 348u, 349u, 350u, 351u, 352u, 156u, 353u, 158u, 159u,
    354u, 355u, 356u, 163u, 164u, 357u, 166u, 167u, 358u, 359u, 170u, 360u, 361u, 362u, 363u, 364u,
    365u, 366u, 367u, 368u, 180u, 369u, 370u, 371u, 372u, 373u, 374u, 187u, 188u, 375u, 376u, 377u,
    378u, 379u, 194u, 380u, 381u, 197u, 382u, 383u, 384u, 201u, 385u, 203u, 386u, 387u, 206u, 388u,
    250u, 251u, 252u, 253u,
};

constexpr std::array<std::uint16_t, 212> material_constant_variant_bucket_5 = {
    0u, 1u, 261u, 262u, 389u, 264u, 6u, 7u, 265u, 240u, 213u, 214u, 241u, 242u, 14u, 215u,
    16u, 17u, 266u, 267u, 268u, 269u, 22u, 270u, 271u, 25u, 216u, 272u, 273u, 274u, 275u, 276u,
    277u, 278u, 279u, 280u, 281u, 282u, 283u, 284u, 285u, 286u, 217u, 218u, 287u, 254u, 46u, 288u,
    289u, 290u, 291u, 51u, 292u, 293u, 294u, 295u, 56u, 296u, 58u, 297u, 60u, 298u, 62u, 63u,
    299u, 300u, 301u, 302u, 303u, 304u, 305u, 306u, 72u, 307u, 74u, 308u, 309u, 310u, 78u, 311u,
    312u, 313u, 314u, 220u, 315u, 316u, 317u, 87u, 88u, 89u, 90u, 91u, 222u, 93u, 244u, 245u,
    246u, 318u, 319u, 99u, 100u, 101u, 320u, 321u, 322u, 105u, 323u, 324u, 325u, 109u, 326u, 111u,
    327u, 328u, 329u, 226u, 116u, 330u, 390u, 119u, 332u, 121u, 122u, 123u, 124u, 125u, 333u, 334u,
    335u, 336u, 337u, 131u, 132u, 133u, 134u, 338u, 339u, 340u, 255u, 248u, 256u, 141u, 341u, 143u,
    342u, 145u, 343u, 344u, 345u, 346u, 347u, 348u, 349u, 350u, 351u, 352u, 156u, 353u, 158u, 231u,
    354u, 355u, 356u, 163u, 164u, 357u, 232u, 167u, 358u, 359u, 170u, 360u, 361u, 362u, 363u, 364u,
    365u, 366u, 367u, 368u, 180u, 369u, 370u, 371u, 372u, 373u, 374u, 187u, 233u, 375u, 376u, 377u,
    378u, 379u, 194u, 391u, 392u, 197u, 382u, 383u, 384u, 201u, 385u, 203u, 386u, 387u, 206u, 388u,
    257u, 258u, 259u, 260u,
};

constexpr std::span<const std::uint16_t> material_constant_variant_buckets[24] = {
    material_constant_variant_bucket_0, // ID 0: SHQ0, versionCap0, cubeCap0, shadow0
    material_constant_variant_bucket_0, // ID 1: SHQ0, versionCap0, cubeCap0, shadow1
    material_constant_variant_bucket_1, // ID 2: SHQ0, versionCap0, cubeCap1, shadow0
    material_constant_variant_bucket_1, // ID 3: SHQ0, versionCap0, cubeCap1, shadow1
    material_constant_variant_bucket_0, // ID 4: SHQ0, versionCap1, cubeCap0, shadow0
    material_constant_variant_bucket_0, // ID 5: SHQ0, versionCap1, cubeCap0, shadow1
    material_constant_variant_bucket_1, // ID 6: SHQ0, versionCap1, cubeCap1, shadow0
    material_constant_variant_bucket_1, // ID 7: SHQ0, versionCap1, cubeCap1, shadow1
    material_constant_variant_bucket_0, // ID 8: SHQ1, versionCap0, cubeCap0, shadow0
    material_constant_variant_bucket_0, // ID 9: SHQ1, versionCap0, cubeCap0, shadow1
    material_constant_variant_bucket_1, // ID 10: SHQ1, versionCap0, cubeCap1, shadow0
    material_constant_variant_bucket_1, // ID 11: SHQ1, versionCap0, cubeCap1, shadow1
    material_constant_variant_bucket_2, // ID 12: SHQ1, versionCap1, cubeCap0, shadow0
    material_constant_variant_bucket_2, // ID 13: SHQ1, versionCap1, cubeCap0, shadow1
    material_constant_variant_bucket_3, // ID 14: SHQ1, versionCap1, cubeCap1, shadow0
    material_constant_variant_bucket_3, // ID 15: SHQ1, versionCap1, cubeCap1, shadow1
    material_constant_variant_bucket_0, // ID 16: SHQ2, versionCap0, cubeCap0, shadow0
    material_constant_variant_bucket_0, // ID 17: SHQ2, versionCap0, cubeCap0, shadow1
    material_constant_variant_bucket_1, // ID 18: SHQ2, versionCap0, cubeCap1, shadow0
    material_constant_variant_bucket_1, // ID 19: SHQ2, versionCap0, cubeCap1, shadow1
    material_constant_variant_bucket_4, // ID 20: SHQ2, versionCap1, cubeCap0, shadow0
    material_constant_variant_bucket_4, // ID 21: SHQ2, versionCap1, cubeCap0, shadow1
    material_constant_variant_bucket_5, // ID 22: SHQ2, versionCap1, cubeCap1, shadow0
    material_constant_variant_bucket_5, // ID 23: SHQ2, versionCap1, cubeCap1, shadow1
};
