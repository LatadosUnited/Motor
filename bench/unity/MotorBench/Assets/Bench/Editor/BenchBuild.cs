// Comparação com o Motor (bench/README.md): deixa o URP com as mesmas configurações do Motor, monta a cena de
// medição e gera o player de Windows em bench/unity/Build/.
// Linha de comando: Unity.exe -batchmode -projectPath bench/unity/MotorBench -executeMethod BenchBuild.MontarECompilar -quit
using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

public static class BenchBuild
{
    const string Pasta = "Assets/Bench";

    [MenuItem("Bench/Montar e compilar")]
    public static void MontarECompilar()
    {
        ConfigurarUrp();
        string cena = CriarCena();
        ConfigurarPlayer();
        var r = BuildPipeline.BuildPlayer(new BuildPlayerOptions
        {
            scenes = new[] { cena },
            target = BuildTarget.StandaloneWindows64,
            locationPathName = "../Build/MotorBench.exe",
            options = BuildOptions.None,
        });
        Debug.Log("BENCH BUILD: " + r.summary.result + " " + r.summary.totalSize + " bytes");
        if (Application.isBatchMode) EditorApplication.Exit(r.summary.result == BuildResult.Succeeded ? 0 : 1);
    }

    // Mesmo pipeline do Motor: HDR em 16 bits por canal, MSAA 4x, 4 cascatas de 2048 (qualidade Média), 150 m.
    static void ConfigurarUrp()
    {
        var assets = new HashSet<RenderPipelineAsset>();
        if (GraphicsSettings.defaultRenderPipeline) assets.Add(GraphicsSettings.defaultRenderPipeline);
        for (int i = 0; i < QualitySettings.names.Length; i++)
        {
            var a = QualitySettings.GetRenderPipelineAssetAt(i);
            if (a) assets.Add(a);
        }
        foreach (var a in assets)
        {
            var so = new SerializedObject(a);
            Set(so, "m_MSAA", 4);
            Set(so, "m_SupportsHDR", true);
            Set(so, "m_HDRColorBufferPrecision", 1);  // 64 bits (RGBA16F), como o Motor
            Set(so, "m_RenderScale", 1f);
            Set(so, "m_MainLightShadowsSupported", true);
            Set(so, "m_MainLightShadowmapResolution", 2048);
            Set(so, "m_ShadowDistance", 150f);
            Set(so, "m_ShadowCascadeCount", 4);
            Set(so, "m_SoftShadowsSupported", true);
            Set(so, "m_AdditionalLightsRenderingMode", 0);  // desligadas: a cena só tem o sol
            // O Motor não faz estas passadas: texturas de profundidade e de cor opaca só servem a efeitos que
            // a cena não usa.
            Set(so, "m_RequireDepthTexture", false);
            Set(so, "m_RequireOpaqueTexture", false);
            so.ApplyModifiedPropertiesWithoutUndo();
            EditorUtility.SetDirty(a);
            Debug.Log("BENCH: URP configurado em " + AssetDatabase.GetAssetPath(a));
        }
        // O template liga o SSAO no renderer de PC; o Motor ainda não tem SSAO (alpha 3, Fase 28).
        foreach (string guid in AssetDatabase.FindAssets("t:UniversalRendererData"))
        {
            var dados = AssetDatabase.LoadAssetAtPath<UniversalRendererData>(AssetDatabase.GUIDToAssetPath(guid));
            foreach (var recurso in dados.rendererFeatures)
            {
                if (!recurso || !recurso.isActive) continue;
                recurso.SetActive(false);
                EditorUtility.SetDirty(recurso);
                Debug.Log("BENCH: recurso do renderer desligado: " + recurso.name + " em " + dados.name);
            }
        }
        AssetDatabase.SaveAssets();
    }

    static void Set(SerializedObject so, string nome, object valor)
    {
        var p = so.FindProperty(nome);
        if (p == null) { Debug.LogWarning("BENCH: propriedade não encontrada: " + nome); return; }
        switch (valor)
        {
            case bool b: p.boolValue = b; break;
            case int i: p.intValue = i; break;
            case float f: p.floatValue = f; break;
        }
    }

    static Material Material(string nome, Color cor)
    {
        string caminho = Pasta + "/" + nome + ".mat";
        var m = AssetDatabase.LoadAssetAtPath<Material>(caminho);
        if (!m)
        {
            m = new Material(Shader.Find("Universal Render Pipeline/Lit"));
            AssetDatabase.CreateAsset(m, caminho);
        }
        m.SetColor("_BaseColor", cor);
        m.SetFloat("_Smoothness", 0.5f);
        EditorUtility.SetDirty(m);
        return m;
    }

    static string CriarCena()
    {
        var cena = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        RenderSettings.fog = false;
        RenderSettings.ambientMode = AmbientMode.Skybox;

        var cam = new GameObject("Câmera").AddComponent<Camera>();
        cam.tag = "MainCamera";
        cam.allowHDR = true;
        cam.allowMSAA = true;
        var dados = cam.gameObject.AddComponent<UniversalAdditionalCameraData>();
        dados.renderPostProcessing = true;
        dados.antialiasing = AntialiasingMode.FastApproximateAntialiasing;

        var sol = new GameObject("Sol").AddComponent<Light>();
        sol.type = LightType.Directional;
        sol.transform.rotation = Quaternion.Euler(50f, -30f, 0f);
        sol.shadows = LightShadows.Soft;

        string caminhoPerfil = Pasta + "/BenchVolume.asset";
        var perfil = AssetDatabase.LoadAssetAtPath<VolumeProfile>(caminhoPerfil);
        if (!perfil)
        {
            perfil = ScriptableObject.CreateInstance<VolumeProfile>();
            AssetDatabase.CreateAsset(perfil, caminhoPerfil);
            var bloom = perfil.Add<Bloom>(true);
            bloom.intensity.Override(0.5f);
            bloom.threshold.Override(1f);
            var tom = perfil.Add<Tonemapping>(true);
            tom.mode.Override(TonemappingMode.ACES);
            foreach (var c in perfil.components) AssetDatabase.AddObjectToAsset(c, perfil);
            EditorUtility.SetDirty(perfil);
        }
        var volume = new GameObject("Pós-processamento").AddComponent<Volume>();
        volume.isGlobal = true;
        volume.sharedProfile = perfil;

        var runner = new GameObject("Bench").AddComponent<BenchRunner>();
        runner.materialCubo = Material("Cubo", new Color(0.3f, 0.6f, 0.95f));
        runner.materialCorpo = Material("Corpo", new Color(0.95f, 0.6f, 0.3f));
        runner.materialChao = Material("Chao", new Color(0.8f, 0.8f, 0.8f));
        runner.sol = sol;
        runner.cameraJogo = cam;

        AssetDatabase.SaveAssets();
        string caminho = Pasta + "/Bench.unity";
        EditorSceneManager.SaveScene(cena, caminho);
        return caminho;
    }

    static void ConfigurarPlayer()
    {
        PlayerSettings.fullScreenMode = FullScreenMode.Windowed;
        PlayerSettings.defaultScreenWidth = 1280;
        PlayerSettings.defaultScreenHeight = 720;
        PlayerSettings.resizableWindow = false;
        PlayerSettings.runInBackground = true;
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.Mono2x);
        EditorUserBuildSettings.development = false;
    }
}
