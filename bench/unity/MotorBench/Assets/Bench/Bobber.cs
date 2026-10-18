// Comparação com o Motor, cena "scripts1000": equivalente ao bench/motor/Scripts/Bobber.cpp.
using UnityEngine;

public class Bobber : MonoBehaviour
{
    Vector3 baseDaPosicao;
    float fase;

    void Start()
    {
        baseDaPosicao = transform.position;
        fase = baseDaPosicao.x * 0.1f + baseDaPosicao.z * 0.07f;
    }

    void Update()
    {
        var p = baseDaPosicao;
        p.y = baseDaPosicao.y + Mathf.Sin(Time.time * 2f + fase) * 0.5f;
        transform.position = p;
    }
}
