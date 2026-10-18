// Comparação com o Motor (bench/README.md): monta por código a mesma cena que bench/gerar_motor.py gera para o
// Motor e mede o tempo de quadro do mesmo jeito (médias de 1 s depois do aquecimento, mediana delas).
// Uso: MotorBench.exe -cena <nome> [-saida <csv>]
using System;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

public class BenchRunner : MonoBehaviour
{
    public Material materialCubo;
    public Material materialCorpo;
    public Material materialChao;
    public Light sol;
    public Camera cameraJogo;

    const float Espaco = 2.5f;
    const float Aquecimento = 4f;
    const int Amostras = 8;

    string cena = "vazia";
    string saida;
    readonly List<double> medias = new List<double>();
    double inicioJanela;
    int quadrosJanela;

    void Awake()
    {
        QualitySettings.vSyncCount = 0;
        Application.targetFrameRate = -1;
        Application.runInBackground = true;
        var args = Environment.GetCommandLineArgs();
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (args[i] == "-cena") cena = args[i + 1];
            if (args[i] == "-saida") saida = args[i + 1];
        }
        if (saida == null) saida = Path.Combine(Path.GetDirectoryName(Application.dataPath), "resultados.csv");
        Screen.SetResolution(1280, 720, FullScreenMode.Windowed);
        Montar();
    }

    static List<Vector2> Grade(int n)
    {
        int lado = Mathf.CeilToInt(Mathf.Sqrt(n));
        float meio = (lado - 1) * Espaco / 2f;
        var r = new List<Vector2>(n);
        for (int k = 0; k < n; k++) r.Add(new Vector2((k % lado) * Espaco - meio, (k / lado) * Espaco - meio));
        return r;
    }

    GameObject Cubo(Vector3 pos, Material mat, bool colisor)
    {
        var go = GameObject.CreatePrimitive(PrimitiveType.Cube);
        if (!colisor) DestroyImmediate(go.GetComponent<Collider>());
        go.transform.position = pos;
        go.GetComponent<MeshRenderer>().sharedMaterial = mat;
        return go;
    }

    void Montar()
    {
        int n = cena switch { "cubos1000" => 1000, "cubos1000sombra" => 1000, "cubos10000" => 10000, "scripts1000" => 1000, _ => 0 };
        sol.shadows = cena == "cubos1000sombra" ? LightShadows.Soft : LightShadows.None;

        if (cena != "vazia")
        {
            float ladoChao = cena == "cubos10000" ? 260f : 90f;
            var chao = GameObject.CreatePrimitive(PrimitiveType.Plane);  // 10 x 10 unidades
            DestroyImmediate(chao.GetComponent<Collider>());
            chao.transform.localScale = new Vector3(ladoChao / 10f, 1f, ladoChao / 10f);
            chao.GetComponent<MeshRenderer>().sharedMaterial = materialChao;
            if (cena == "fisica1000")
            {
                var box = chao.AddComponent<BoxCollider>();
                box.size = new Vector3(10f, 1f, 10f);
                box.center = new Vector3(0f, -0.5f, 0f);
            }
        }

        if (n > 0)
        {
            foreach (var p in Grade(n))
            {
                var go = Cubo(new Vector3(p.x, 0.5f, p.y), materialCubo, false);
                if (cena == "scripts1000") go.AddComponent<Bobber>();
            }
        }

        if (cena == "fisica1000")
        {
            for (int k = 0; k < 1000; k++)
            {
                int i = k % 10, j = (k / 10) % 10, h = k / 100;
                float meio = h % 2 == 1 ? 0.6f : 0f;  // camadas desalinhadas, como no gerar_motor.py
                var go = Cubo(new Vector3((i - 4.5f) * 1.2f + meio, 8f + h * 1.6f, (j - 4.5f) * 1.2f + meio), materialCorpo, true);
                var rb = go.AddComponent<Rigidbody>();
                rb.mass = 1f;
                rb.linearDamping = 0.05f;
                rb.angularDamping = 0.05f;
            }
        }

        if (cena == "spawn200")
        {
            var modelo = Cubo(new Vector3(0f, -10f, 0f), materialCubo, false);
            var spawner = new GameObject("Gerador").AddComponent<Spawner>();
            spawner.modelo = modelo;
        }

        // O Motor olha para -Z; aqui a câmera fica do outro lado olhando para +Z (a cena é simétrica).
        bool grande = cena == "cubos10000";
        cameraJogo.transform.position = new Vector3(0f, grande ? 110f : 45f, grande ? -210f : -85f);
        cameraJogo.transform.rotation = Quaternion.Euler(30f, 0f, 0f);
        cameraJogo.farClipPlane = 600f;
        cameraJogo.fieldOfView = 60f;
    }

    void Update()
    {
        double agora = Time.realtimeSinceStartupAsDouble;
        if (agora < Aquecimento) return;
        if (inicioJanela == 0) { inicioJanela = agora; quadrosJanela = 0; return; }
        quadrosJanela++;
        if (agora - inicioJanela < 1.0) return;
        medias.Add((agora - inicioJanela) * 1000.0 / quadrosJanela);
        inicioJanela = agora;
        quadrosJanela = 0;
        if (medias.Count < Amostras) return;

        medias.Sort();
        double ms = medias[medias.Count / 2];
        bool novo = !File.Exists(saida);
        using (var w = new StreamWriter(saida, true))
        {
            if (novo) w.WriteLine("cena,ms,fps,amostras,resolucao");
            w.WriteLine(string.Format(System.Globalization.CultureInfo.InvariantCulture, "{0},{1:F2},{2:F1},{3},{4}x{5}",
                cena, ms, 1000.0 / ms, medias.Count, Screen.width, Screen.height));
        }
        Application.Quit();
    }
}
