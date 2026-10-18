// Comparação com o Motor, cena "spawn200": equivalente ao bench/motor/Scripts/Spawner.cpp (mesmo sorteio).
using System.Collections.Generic;
using UnityEngine;

public class Spawner : MonoBehaviour
{
    public GameObject modelo;
    public int quantidade = 200;

    readonly List<GameObject> vivos = new List<GameObject>();
    uint semente = 1;

    float Sorteio()
    {
        semente = semente * 1664525u + 1013904223u;
        return (semente >> 8) / 16777216f;
    }

    void Update()
    {
        foreach (var go in vivos) Destroy(go);
        vivos.Clear();
        for (int i = 0; i < quantidade; i++)
        {
            var go = Instantiate(modelo, new Vector3(Sorteio() * 80f - 40f, 0.5f, Sorteio() * 80f - 40f), Quaternion.identity);
            vivos.Add(go);
        }
    }
}
