#ifndef NODE_ITEM_H
#define NODE_ITEM_H

#include <QGraphicsItem>
#include <QString>
#include <QStringList>
#include <QSet>

namespace graph_studio {

class EdgeItem;

class NodeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };
    enum class PortDir { None, Input, Output };
    enum class RunStatus { None, Running, Completed, Failed };

    NodeItem(const QString& nodeId, const QString& nodeType, QGraphicsItem* parent = nullptr);
    ~NodeItem() override;

    QString nodeId() const;
    QString nodeType() const;
    void setNodeType(const QString& type);

    // 端口契约：节点声明的输入/输出端口名。空列表 → 渲染单个默认 in/out 锚点。
    void setPorts(const QStringList& inputs, const QStringList& outputs);
    QStringList inputPorts() const;
    QStringList outputPorts() const;

    // 执行状态：驱动节点边框/发光配色（运行中/成功/失败）
    void setRunStatus(RunStatus status);
    RunStatus runStatus() const;

    // Port geometry in scene coordinates
    QPointF inputPortPos(const QString& name) const;
    QPointF outputPortPos(const QString& name) const;

    // 命中测试：返回命中方向 + 具体端口名（未命中返回 None / 空串）
    PortDir hitPort(const QPointF& scenePos) const;
    QString hitPortName(const QPointF& scenePos) const;
    QString inputPortNameAt(const QPointF& scenePos) const;
    QString outputPortNameAt(const QPointF& scenePos) const;

    // Highlight target input port during edge-drag (drop target feedback)
    void setDropHighlighted(bool on);

    // Edge registration for movement tracking
    void registerEdge(EdgeItem* edge);
    void unregisterEdge(EdgeItem* edge);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    int type() const override;

    static qreal nodeWidth() { return NODE_WIDTH; }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    void notifyEdges();
    qreal height() const;
    // 端口在节点本地坐标的位置。dir: 0=输入(左) 1=输出(右)
    QPointF portLocalPos(int dir, int index, int count) const;
    bool hitPortAt(const QPointF& scenePos, int dir, const QStringList& ports, QString& name) const;
    // 端口名 -> (dir,index) 映射的端口名解析
    QStringList effectiveInput() const;   // 空->{"in"}
    QStringList effectiveOutput() const;  // 空->{"out"}
    void drawPort(QPainter* painter, const QPointF& center, bool active, bool dropRing);
    void drawPortLabel(QPainter* painter, const QPointF& center, const QString& name, bool input);

    QString nodeId_;
    QString nodeType_;
    QStringList inputPorts_;
    QStringList outputPorts_;
    QSet<EdgeItem*> edges_;
    RunStatus runStatus_ = RunStatus::None;
    PortDir hoverPort_ = PortDir::None;
    QString hoverPortName_;
    bool dropHighlighted_ = false;

    static const qreal NODE_WIDTH;
    static const qreal NODE_HEIGHT;   // 最小高度（多端口时自动增高）
    static const qreal PORT_RADIUS;
    static const qreal PORT_HIT_RADIUS;
};

} // namespace graph_studio

#endif // NODE_ITEM_H